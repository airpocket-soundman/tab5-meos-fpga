/* MicroPython `camera` module: the built-in camera and a USB (UVC) camera behind one interface (user/camera/
 * camsrc.c), BMP files and LCD display.
 *
 *   cam = camera.Camera(camera.USB)         # or camera.ONBOARD, "usb", "onboard"
 *   img = cam.read()                        # Image, scaled to fit the GC heap
 *   img.show(hold_ms=2000); img.save("/sd/a.bmp")
 *   cam.live(5000); cam.close()
 *
 * Pixels are RGB565 little-endian in a bytearray on the GC heap (Image.data), so an image needs width*height*2
 * bytes of that heap.  Showing an image pauses the text console; it stays until camera.close() (or VM exit)
 * unless hold_ms is given. */
#include "machine_meos.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/runtime.h"
#include <m5os/board_config.h>
#include <m5os/camera.h>
#include <m5os/devreg.h>
#include <m5os/display.h>
#include <m5os/hal_time.h>
#include <m5os/kmalloc.h>
#include <m5os/task.h>
#include <string.h>
#ifndef M5OS_MODULE_MP_CAMERA
#define M5OS_MODULE_MP_CAMERA 1
#endif
#if M5OS_BOARD_TAB5 && M5OS_MODULE_MP_CAMERA
#include "../../drivers/lcd/fbcon.h"
#include "../camera/camera_app.h"
#include "../camera/camsrc.h"

#define SHOW_MAX_W 960
#define SHOW_MAX_H 540
#define IMAGE_MAX_W 1920
#define IMAGE_MAX_H 1080
#define AUTO_MAX_W 400 /* read() without a size: the largest integer reduction that fits this box */
#define AUTO_MAX_H 300

typedef struct {
  mp_obj_base_t base;
  mp_int_t width, height;
  mp_obj_t data; /* bytearray, width*height*2 bytes */
} meos_image_t;
extern const mp_obj_type_t meos_image_type;

typedef struct {
  mp_obj_base_t base;
  camsrc_t *cam; /* NULL once closed */
  camsrc_id_t id;
  int width, height; /* native frame size */
} meos_camera_t;
extern const mp_obj_type_t meos_camera_type;

static camsrc_t *s_python_cams[CAMSRC_COUNT]; /* closed at VM exit */
static uint16_t *s_live_buffer;               /* PSRAM, reused by Camera.live() */
static bool s_screen_taken; /* console paused by show()/live() */
static bool s_console_was_paused;

static meos_image_t *image_object(mp_obj_t obj) {
  if (!mp_obj_is_type(obj, &meos_image_type))
    mp_raise_TypeError(MP_ERROR_TEXT("expected camera.Image"));
  return MP_OBJ_TO_PTR(obj);
}

static meos_camera_t *camera_object(mp_obj_t obj) {
  if (!mp_obj_is_type(obj, &meos_camera_type))
    mp_raise_TypeError(MP_ERROR_TEXT("expected camera.Camera"));
  return MP_OBJ_TO_PTR(obj);
}

static camsrc_t *camera_live_handle(meos_camera_t *self) {
  if (!self->cam)
    mp_raise_OSError(MP_EBADF);
  return self->cam;
}

static uint16_t *image_pixels(meos_image_t *img, mp_uint_t flags) {
  mp_buffer_info_t info;
  mp_get_buffer_raise(img->data, &info, flags);
  if (info.len < (size_t)img->width * (size_t)img->height * 2u)
    mp_raise_ValueError(MP_ERROR_TEXT("image data too small"));
  return info.buf;
}

/* Image with zeroed pixels; MemoryError when the GC heap cannot hold them. */
static meos_image_t *image_alloc(mp_int_t w, mp_int_t h) {
  if (w < 1 || h < 1 || w > IMAGE_MAX_W || h > IMAGE_MAX_H)
    mp_raise_ValueError(MP_ERROR_TEXT("bad image size"));
  size_t bytes = (size_t)w * (size_t)h * 2u;
  uint8_t *pixels = m_new_maybe(uint8_t, bytes);
  if (!pixels)
    mp_raise_msg_varg(&mp_type_MemoryError, MP_ERROR_TEXT("image needs %u bytes"), (unsigned)bytes);
  memset(pixels, 0, bytes);
  meos_image_t *img = mp_obj_malloc(meos_image_t, &meos_image_type);
  img->width = w;
  img->height = h;
  img->data = mp_obj_new_bytearray_by_ref(bytes, pixels);
  return img;
}

/* Every scale-th pixel of a kmalloc'ed frame into a new Image; frees `src`. */
static mp_obj_t image_from_frame(uint16_t *src, int w, int h, mp_int_t scale) {
  if (scale < 1 || scale > 16) {
    kfree(src);
    mp_raise_ValueError(MP_ERROR_TEXT("scale must be 1..16"));
  }
  int dw = w / (int)scale, dh = h / (int)scale;
  if (dw < 1 || dh < 1) {
    kfree(src);
    mp_raise_ValueError(MP_ERROR_TEXT("scale too large"));
  }
  size_t bytes = (size_t)dw * (size_t)dh * 2u;
  uint8_t *pixels = m_new_maybe(uint8_t, bytes);
  if (!pixels) {
    kfree(src);
    mp_raise_msg_varg(&mp_type_MemoryError, MP_ERROR_TEXT("image needs %u bytes"), (unsigned)bytes);
  }
  uint16_t *dst = (uint16_t *)pixels;
  for (int y = 0; y < dh; y++)
    for (int x = 0; x < dw; x++)
      dst[(size_t)y * dw + x] = src[(size_t)(y * scale) * w + (x * scale)];
  kfree(src);
  meos_image_t *img = mp_obj_malloc(meos_image_t, &meos_image_type);
  img->width = dw;
  img->height = dh;
  img->data = mp_obj_new_bytearray_by_ref(bytes, pixels);
  return MP_OBJ_FROM_PTR(img);
}

static void require_absolute(const char *path) {
  if (path[0] != '/')
    mp_raise_ValueError(MP_ERROR_TEXT("path must be absolute"));
}

/* ---- screen ownership ---- */

static void console_resume(void) {
  if (s_screen_taken) {
    fbcon_pause(s_console_was_paused);
    s_screen_taken = false;
  }
}

static void screen_take(void) {
  if (!s_screen_taken) {
    s_console_was_paused = fbcon_is_paused();
    fbcon_pause(true);
    s_screen_taken = true;
    task_sleep(80); /* let the console finish its last repaint */
  }
}

typedef struct {
  int x, y, w, h, hold_ms;
  bool whole_screen;
} area_t;

static area_t parse_area(size_t n_args, const mp_obj_t *args, mp_map_t *kw, mp_int_t default_hold) {
  enum { ARG_x, ARG_y, ARG_w, ARG_h, ARG_hold_ms };
  static const mp_arg_t allowed[] = {
      {MP_QSTR_x, MP_ARG_INT, {.u_int = 0}},
      {MP_QSTR_y, MP_ARG_INT, {.u_int = 0}},
      {MP_QSTR_w, MP_ARG_INT, {.u_int = DISPLAY_WIDTH}},
      {MP_QSTR_h, MP_ARG_INT, {.u_int = DISPLAY_HEIGHT}},
      {MP_QSTR_hold_ms, MP_ARG_INT, {.u_int = 0}},
  };
  mp_arg_val_t v[MP_ARRAY_SIZE(allowed)];
  mp_arg_parse_all(n_args, args, kw, MP_ARRAY_SIZE(allowed), allowed, v);
  area_t a = {v[ARG_x].u_int, v[ARG_y].u_int, v[ARG_w].u_int, v[ARG_h].u_int, v[ARG_hold_ms].u_int, false};
  (void)default_hold;
  if (a.w < 1 || a.h < 1 || a.x < 0 || a.y < 0 || a.x + a.w > DISPLAY_WIDTH || a.y + a.h > DISPLAY_HEIGHT)
    mp_raise_ValueError(MP_ERROR_TEXT("area outside the screen"));
  if (a.hold_ms < 0 || a.hold_ms > 600000)
    mp_raise_ValueError(MP_ERROR_TEXT("hold_ms must be 0..600000"));
  a.whole_screen = a.x == 0 && a.y == 0 && a.w == DISPLAY_WIDTH && a.h == DISPLAY_HEIGHT;
  return a;
}

static void show_image(meos_image_t *img, size_t n_args, const mp_obj_t *args, mp_map_t *kw) {
  area_t a = parse_area(n_args, args, kw, 0);
  const uint16_t *px = image_pixels(img, MP_BUFFER_READ);
  screen_take();
  bool batch = display_begin_frame();
  if (a.whole_screen)
    display_clear(COLOR_BLACK);
  camsrc_draw_fit(px, (int)img->width, (int)img->height, a.x, a.y, a.w, a.h);
  if (batch)
    display_end_frame();
  if (a.hold_ms > 0) {
    mp_hal_delay_ms(a.hold_ms);
    console_resume();
  }
}

/* ---- Image ---- */

/* Image(width, height): black image to draw into. */
static mp_obj_t image_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw,
                               const mp_obj_t *args) {
  (void)type;
  mp_arg_check_num(n_args, n_kw, 2, 2, false);
  return MP_OBJ_FROM_PTR(image_alloc(mp_obj_get_int(args[0]), mp_obj_get_int(args[1])));
}

static void image_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
  meos_image_t *self = MP_OBJ_TO_PTR(self_in);
  if (dest[0] != MP_OBJ_NULL) {
    return; /* read-only attributes */
  }
  if (attr == MP_QSTR_width)
    dest[0] = mp_obj_new_int(self->width);
  else if (attr == MP_QSTR_height)
    dest[0] = mp_obj_new_int(self->height);
  else if (attr == MP_QSTR_data)
    dest[0] = self->data;
  else
    dest[1] = MP_OBJ_SENTINEL; /* methods: look in locals_dict */
}

static mp_obj_t image_show(size_t n_args, const mp_obj_t *args, mp_map_t *kw) {
  show_image(image_object(args[0]), n_args - 1, args + 1, kw);
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(image_show_obj, 1, image_show);

static mp_obj_t image_save(mp_obj_t self_in, mp_obj_t path_in) {
  meos_image_t *self = image_object(self_in);
  const char *path = mp_obj_str_get_str(path_in);
  require_absolute(path);
  if (!camera_image_save_rgb565(path, image_pixels(self, MP_BUFFER_READ), (int)self->width, (int)self->height))
    mp_raise_OSError(MP_EIO);
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(image_save_obj, image_save);

static const mp_rom_map_elem_t image_locals[] = {
    {MP_ROM_QSTR(MP_QSTR_show), MP_ROM_PTR(&image_show_obj)},
    {MP_ROM_QSTR(MP_QSTR_save), MP_ROM_PTR(&image_save_obj)},
};
static MP_DEFINE_CONST_DICT(image_locals_dict, image_locals);
MP_DEFINE_CONST_OBJ_TYPE(meos_image_type, MP_QSTR_Image, MP_TYPE_FLAG_NONE, make_new, image_make_new, attr,
                         image_attr, locals_dict, &image_locals_dict);

/* ---- Camera ---- */

static camsrc_id_t source_from_obj(mp_obj_t obj) {
  camsrc_id_t id;
  if (mp_obj_is_str(obj)) {
    const char *name = mp_obj_str_get_str(obj);
    if (camsrc_find(name, &id) || camsrc_resolve(name, &id))   /* "usb", "camera1" or "/dev/camera1" */
      return id;
    mp_raise_ValueError(MP_ERROR_TEXT("no such camera"));
  }
  mp_int_t n = mp_obj_get_int(obj);
  if (n < 0 || n >= CAMSRC_COUNT)
    mp_raise_ValueError(MP_ERROR_TEXT("source must be camera.ONBOARD or camera.USB"));
  return (camsrc_id_t)n;
}

/* Camera(source=ONBOARD): opens the camera; OSError if it is absent or busy. */
static mp_obj_t camera_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw,
                                const mp_obj_t *args) {
  (void)type;
  mp_arg_check_num(n_args, n_kw, 0, 1, false);
  camsrc_id_t id = n_args ? source_from_obj(args[0]) : CAMSRC_ONBOARD;
  int w = 0, h = 0;
  if (!camsrc_native_size(id, &w, &h))
    mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("camera not present"));
  camsrc_t *cam = camsrc_open(id);
  if (!cam)
    mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("camera busy or failed to start"));
  meos_camera_t *self = mp_obj_malloc(meos_camera_t, &meos_camera_type);
  self->cam = cam;
  self->id = id;
  self->width = w;
  self->height = h;
  s_python_cams[id] = cam;
  return MP_OBJ_FROM_PTR(self);
}

static void camera_close_object(meos_camera_t *self) {
  if (self->cam) {
    s_python_cams[self->id] = NULL;
    camsrc_close(self->cam);
    self->cam = NULL;
  }
}

static void camera_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
  meos_camera_t *self = MP_OBJ_TO_PTR(self_in);
  if (dest[0] != MP_OBJ_NULL) {
    return;
  }
  if (attr == MP_QSTR_source)
    dest[0] = mp_obj_new_int(self->id);
  else if (attr == MP_QSTR_name)
    dest[0] = mp_obj_new_str(camsrc_name(self->id), strlen(camsrc_name(self->id)));
  else if (attr == MP_QSTR_width)
    dest[0] = mp_obj_new_int(self->width);
  else if (attr == MP_QSTR_height)
    dest[0] = mp_obj_new_int(self->height);
  else
    dest[1] = MP_OBJ_SENTINEL;
}

/* Output size for read(): both zero = the smallest integer reduction that fits 400x300; one given = keep
 * the aspect ratio. */
static void read_size(meos_camera_t *self, mp_int_t width, mp_int_t height, int *w, int *h) {
  if (width < 0 || height < 0)
    mp_raise_ValueError(MP_ERROR_TEXT("bad size"));
  if (!width && !height) {
    int k = 1;
    while (self->width / k > AUTO_MAX_W || self->height / k > AUTO_MAX_H)
      k++;
    *w = self->width / k;
    *h = self->height / k;
  } else if (!height) {
    *w = (int)width;
    *h = (int)(((int64_t)self->height * width) / self->width);
  } else if (!width) {
    *h = (int)height;
    *w = (int)(((int64_t)self->width * height) / self->height);
  } else {
    *w = (int)width;
    *h = (int)height;
  }
  if (*w < 1) *w = 1;
  if (*h < 1) *h = 1;
}

/* read(width=0, height=0, timeout_ms=10000) -> Image */
static mp_obj_t camera_read(size_t n_args, const mp_obj_t *args, mp_map_t *kw) {
  enum { ARG_width, ARG_height, ARG_timeout_ms };
  static const mp_arg_t allowed[] = {
      {MP_QSTR_width, MP_ARG_INT, {.u_int = 0}},
      {MP_QSTR_height, MP_ARG_INT, {.u_int = 0}},
      {MP_QSTR_timeout_ms, MP_ARG_INT, {.u_int = 10000}},
  };
  meos_camera_t *self = camera_object(args[0]);
  mp_arg_val_t v[MP_ARRAY_SIZE(allowed)];
  mp_arg_parse_all(n_args - 1, args + 1, kw, MP_ARRAY_SIZE(allowed), allowed, v);
  camsrc_t *cam = camera_live_handle(self);
  int timeout = v[ARG_timeout_ms].u_int;
  if (timeout < 100 || timeout > 60000)
    mp_raise_ValueError(MP_ERROR_TEXT("timeout_ms must be 100..60000"));
  int w, h;
  read_size(self, v[ARG_width].u_int, v[ARG_height].u_int, &w, &h);
  meos_image_t *img = image_alloc(w, h);
  camsrc_set_limit(cam, w, h);
  camsrc_frame_t f;
  if (!camsrc_grab(cam, &f, (uint32_t)timeout))
    mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("no frame from the camera"));
  camsrc_scale(&f, image_pixels(img, MP_BUFFER_WRITE), w, h);
  return MP_OBJ_FROM_PTR(img);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(camera_read_obj, 1, camera_read);

/* read_into(image, timeout_ms=10000): no allocation; scaled to the image's size. */
static mp_obj_t camera_read_into(size_t n_args, const mp_obj_t *args, mp_map_t *kw) {
  enum { ARG_image, ARG_timeout_ms };
  static const mp_arg_t allowed[] = {
      {MP_QSTR_image, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_timeout_ms, MP_ARG_INT, {.u_int = 10000}},
  };
  meos_camera_t *self = camera_object(args[0]);
  mp_arg_val_t v[MP_ARRAY_SIZE(allowed)];
  mp_arg_parse_all(n_args - 1, args + 1, kw, MP_ARRAY_SIZE(allowed), allowed, v);
  camsrc_t *cam = camera_live_handle(self);
  meos_image_t *img = image_object(v[ARG_image].u_obj);
  int timeout = v[ARG_timeout_ms].u_int;
  if (timeout < 100 || timeout > 60000)
    mp_raise_ValueError(MP_ERROR_TEXT("timeout_ms must be 100..60000"));
  uint16_t *dst = image_pixels(img, MP_BUFFER_WRITE);
  camsrc_set_limit(cam, (int)img->width, (int)img->height);
  camsrc_frame_t f;
  if (!camsrc_grab(cam, &f, (uint32_t)timeout))
    mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("no frame from the camera"));
  camsrc_scale(&f, dst, (int)img->width, (int)img->height);
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(camera_read_into_obj, 1, camera_read_into);

/* modes() -> [(width, height, "mjpeg"|"yuy2"|"raw", fps), ...]: the modes this camera offers. */
static mp_obj_t camera_modes(mp_obj_t self_in) {
  meos_camera_t *self = camera_object(self_in);
  camsrc_mode_t modes[32];
  int n = camsrc_modes(self->id, modes, 32);
  if (n > 32)
    n = 32;
  mp_obj_t list = mp_obj_new_list(0, NULL);
  for (int i = 0; i < n; i++) {
    const char *fmt = self->id == CAMSRC_ONBOARD ? "raw" : modes[i].mjpeg ? "mjpeg" : "yuy2";
    mp_obj_t item[4] = {mp_obj_new_int(modes[i].width), mp_obj_new_int(modes[i].height),
                        mp_obj_new_str(fmt, strlen(fmt)), mp_obj_new_float((mp_float_t)modes[i].fps10 / 10)};
    mp_obj_list_append(list, mp_obj_new_tuple(4, item));
  }
  return list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(camera_modes_obj, camera_modes);

/* set_size(width, height, format=None): choose one of modes().  format "mjpeg" / "yuy2" forces one; None takes
 * MJPEG when the camera offers it for that size.  width/height then report the new native size. */
static mp_obj_t camera_set_size(size_t n_args, const mp_obj_t *args) {
  meos_camera_t *self = camera_object(args[0]);
  int w = (int)mp_obj_get_int(args[1]), h = (int)mp_obj_get_int(args[2]);
  int fmt = -1;
  if (n_args > 3 && args[3] != mp_const_none) {
    const char *name = mp_obj_str_get_str(args[3]);
    fmt = !strcmp(name, "mjpeg") ? 1 : !strcmp(name, "yuy2") ? 0 : -2;
    if (fmt == -2)
      mp_raise_ValueError(MP_ERROR_TEXT("format must be \"mjpeg\" or \"yuy2\""));
  }
  if (!camsrc_set_size(camera_live_handle(self), w, h, fmt))
    mp_raise_ValueError(MP_ERROR_TEXT("mode not offered by this camera"));
  self->width = w;
  self->height = h;
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(camera_set_size_obj, 3, 4, camera_set_size);

static mp_obj_t camera_set_flip(size_t n_args, const mp_obj_t *args) {
  meos_camera_t *self = camera_object(args[0]);
  camsrc_set_flip(camera_live_handle(self), mp_obj_is_true(args[1]), n_args > 2 && mp_obj_is_true(args[2]));
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(camera_set_flip_obj, 2, 3, camera_set_flip);

/* live(duration_ms=5000, x=0, y=0, w=<screen>, h=<screen>): show frames as they arrive; Ctrl-C stops. */
static mp_obj_t camera_live(size_t n_args, const mp_obj_t *args, mp_map_t *kw) {
  meos_camera_t *self = camera_object(args[0]);
  camsrc_t *cam = camera_live_handle(self);
  mp_int_t duration = 5000;
  size_t rest = n_args - 1;
  const mp_obj_t *rest_args = args + 1;
  if (rest > 0) {
    duration = mp_obj_get_int(rest_args[0]);
    rest--;
    rest_args++;
  }
  if (duration < 100 || duration > 3600000)
    mp_raise_ValueError(MP_ERROR_TEXT("duration_ms must be 100..3600000"));
  area_t a = parse_area(rest, rest_args, kw, 0);
  int bw = a.w < SHOW_MAX_W ? a.w : SHOW_MAX_W, bh = a.h < SHOW_MAX_H ? a.h : SHOW_MAX_H;
  int dw = bw, dh = (int)(((int64_t)self->height * bw) / self->width);
  if (dh > bh) {
    dh = bh;
    dw = (int)(((int64_t)self->width * bh) / self->height);
  }
  if (dw < 1) dw = 1;
  if (dh < 1) dh = 1;
  if (!s_live_buffer) {
    s_live_buffer = kmalloc_psram((size_t)SHOW_MAX_W * SHOW_MAX_H * sizeof(uint16_t));
    if (!s_live_buffer)
      mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("no PSRAM for the live buffer"));
  }
  int dx = a.x + (a.w - dw) / 2, dy = a.y + (a.h - dh) / 2;
  camsrc_set_limit(cam, dw, dh);
  screen_take();
  if (a.whole_screen)
    display_clear(COLOR_BLACK);
  uint64_t end = hal_time_now_us() + (uint64_t)duration * 1000u;
  uint32_t frames = 0;
  while (hal_time_now_us() < end) {
    camsrc_frame_t f;
    if (!camsrc_grab(cam, &f, 5000))
      mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("no frame from the camera"));
    camsrc_scale(&f, s_live_buffer, dw, dh);
    display_draw_bitmap(dx, dy, dw, dh, s_live_buffer);
    frames++;
    meos_mp_poll();
    mp_handle_pending(true); /* Ctrl-C */
  }
  if (a.hold_ms > 0) {
    mp_hal_delay_ms(a.hold_ms);
    console_resume();
  }
  return mp_obj_new_int(frames);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(camera_live_obj, 1, camera_live);

static mp_obj_t camera_object_close(mp_obj_t self_in) {
  camera_close_object(camera_object(self_in));
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(camera_object_close_obj, camera_object_close);

static mp_obj_t camera_enter(mp_obj_t self_in) { return self_in; }
static MP_DEFINE_CONST_FUN_OBJ_1(camera_enter_obj, camera_enter);

static mp_obj_t camera_exit(size_t n_args, const mp_obj_t *args) {
  (void)n_args;
  camera_close_object(camera_object(args[0]));
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(camera_exit_obj, 1, 4, camera_exit);

static const mp_rom_map_elem_t camera_type_locals[] = {
    {MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&camera_read_obj)},
    {MP_ROM_QSTR(MP_QSTR_read_into), MP_ROM_PTR(&camera_read_into_obj)},
    {MP_ROM_QSTR(MP_QSTR_modes), MP_ROM_PTR(&camera_modes_obj)},
    {MP_ROM_QSTR(MP_QSTR_set_size), MP_ROM_PTR(&camera_set_size_obj)},
    {MP_ROM_QSTR(MP_QSTR_set_flip), MP_ROM_PTR(&camera_set_flip_obj)},
    {MP_ROM_QSTR(MP_QSTR_live), MP_ROM_PTR(&camera_live_obj)},
    {MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&camera_object_close_obj)},
    {MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&camera_enter_obj)},
    {MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&camera_exit_obj)},
};
static MP_DEFINE_CONST_DICT(camera_type_locals_dict, camera_type_locals);
MP_DEFINE_CONST_OBJ_TYPE(meos_camera_type, MP_QSTR_Camera, MP_TYPE_FLAG_NONE, make_new, camera_make_new, attr,
                         camera_attr, locals_dict, &camera_type_locals_dict);

/* ---- module functions ---- */

/* One-shot helpers: open, read one frame, close. */
static mp_obj_t one_shot(camsrc_id_t id, int w, int h, uint32_t timeout) {
  int nw = 0, nh = 0;
  if (!camsrc_native_size(id, &nw, &nh))
    mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("camera not present"));
  meos_image_t *img = image_alloc(w, h);
  if (!camsrc_capture(id, image_pixels(img, MP_BUFFER_WRITE), w, h, timeout))
    mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("camera capture failed"));
  return MP_OBJ_FROM_PTR(img);
}

static int list_cameras_cb(const devreg_entry_t *e, void *ctx) {
  if (e->cls == DEVREG_CAMERA)
    mp_obj_list_append((mp_obj_t)ctx, mp_obj_new_str(e->name, strlen(e->name)));
  return 0;
}
/* list() -> ["camera0", "camera1", ...]: the cameras that are registered right now. */
static mp_obj_t camera_list(void) {
  mp_obj_t list = mp_obj_new_list(0, NULL);
  devreg_foreach(list_cameras_cb, list);
  return list;
}
static MP_DEFINE_CONST_FUN_OBJ_0(camera_list_obj, camera_list);

static mp_obj_t camera_available(mp_obj_t source) { return mp_obj_new_bool(camsrc_available(source_from_obj(source))); }
static MP_DEFINE_CONST_FUN_OBJ_1(camera_available_obj, camera_available);

static mp_obj_t camera_usb_available(void) { return mp_obj_new_bool(camsrc_available(CAMSRC_USB)); }
static MP_DEFINE_CONST_FUN_OBJ_0(camera_usb_available_obj, camera_usb_available);

static mp_obj_t camera_usb_capture(size_t n_args, const mp_obj_t *args, mp_map_t *kw) {
  enum { ARG_timeout_ms, ARG_scale };
  static const mp_arg_t allowed[] = {
      {MP_QSTR_timeout_ms, MP_ARG_INT, {.u_int = 10000}},
      {MP_QSTR_scale, MP_ARG_INT, {.u_int = 1}},
  };
  mp_arg_val_t v[MP_ARRAY_SIZE(allowed)];
  mp_arg_parse_all(n_args, args, kw, MP_ARRAY_SIZE(allowed), allowed, v);
  int timeout = v[ARG_timeout_ms].u_int, scale = v[ARG_scale].u_int;
  if (timeout < 100 || timeout > 60000)
    mp_raise_ValueError(MP_ERROR_TEXT("timeout_ms must be 100..60000"));
  if (scale < 1 || scale > 16)
    mp_raise_ValueError(MP_ERROR_TEXT("scale must be 1..16"));
  int nw = 0, nh = 0;
  if (!camsrc_native_size(CAMSRC_USB, &nw, &nh))
    mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("no USB camera"));
  return one_shot(CAMSRC_USB, nw / scale > 0 ? nw / scale : 1, nh / scale > 0 ? nh / scale : 1, (uint32_t)timeout);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(camera_usb_capture_obj, 0, camera_usb_capture);

static mp_obj_t camera_onboard_capture(size_t n_args, const mp_obj_t *args, mp_map_t *kw) {
  enum { ARG_width, ARG_height };
  static const mp_arg_t allowed[] = {
      {MP_QSTR_width, MP_ARG_INT, {.u_int = 320}},
      {MP_QSTR_height, MP_ARG_INT, {.u_int = 180}},
  };
  mp_arg_val_t v[MP_ARRAY_SIZE(allowed)];
  mp_arg_parse_all(n_args, args, kw, MP_ARRAY_SIZE(allowed), allowed, v);
  int w = v[ARG_width].u_int, h = v[ARG_height].u_int;
  if (w < 16 || w > CAMERA_WIDTH || h < 16 || h > CAMERA_HEIGHT)
    mp_raise_ValueError(MP_ERROR_TEXT("size must be 16..1280 x 16..720"));
  return one_shot(CAMSRC_ONBOARD, w, h, 10000);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(camera_onboard_capture_obj, 0, camera_onboard_capture);

static mp_obj_t camera_load(size_t n_args, const mp_obj_t *args, mp_map_t *kw) {
  enum { ARG_path, ARG_scale };
  static const mp_arg_t allowed[] = {
      {MP_QSTR_path, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_scale, MP_ARG_INT, {.u_int = 1}},
  };
  mp_arg_val_t v[MP_ARRAY_SIZE(allowed)];
  mp_arg_parse_all(n_args, args, kw, MP_ARRAY_SIZE(allowed), allowed, v);
  const char *path = mp_obj_str_get_str(v[ARG_path].u_obj);
  require_absolute(path);
  uint16_t *pixels = NULL;
  int w = 0, h = 0;
  if (!camera_load_bmp(path, &pixels, &w, &h))
    mp_raise_OSError(MP_ENOENT);
  return image_from_frame(pixels, w, h, v[ARG_scale].u_int);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(camera_load_obj, 1, camera_load);

static mp_obj_t camera_show(size_t n_args, const mp_obj_t *args, mp_map_t *kw) {
  show_image(image_object(args[0]), n_args - 1, args + 1, kw);
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(camera_show_obj, 1, camera_show);

static mp_obj_t camera_close(void) {
  console_resume();
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(camera_close_obj, camera_close);

/* VM exit: release every camera the script left open, give the console back. */
void mp_camera_deinit(void) {
  for (int i = 0; i < CAMSRC_COUNT; i++) {
    if (s_python_cams[i]) {
      camsrc_close(s_python_cams[i]);
      s_python_cams[i] = NULL;
    }
  }
  kfree(s_live_buffer);
  s_live_buffer = NULL;
  console_resume();
}

static const mp_rom_map_elem_t camera_globals[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_camera)},
    {MP_ROM_QSTR(MP_QSTR_ONBOARD), MP_ROM_INT(CAMSRC_ONBOARD)},
    {MP_ROM_QSTR(MP_QSTR_USB), MP_ROM_INT(CAMSRC_USB)},
    {MP_ROM_QSTR(MP_QSTR_Camera), MP_ROM_PTR(&meos_camera_type)},
    {MP_ROM_QSTR(MP_QSTR_Image), MP_ROM_PTR(&meos_image_type)},
    {MP_ROM_QSTR(MP_QSTR_list), MP_ROM_PTR(&camera_list_obj)},
    {MP_ROM_QSTR(MP_QSTR_available), MP_ROM_PTR(&camera_available_obj)},
    {MP_ROM_QSTR(MP_QSTR_usb_available), MP_ROM_PTR(&camera_usb_available_obj)},
    {MP_ROM_QSTR(MP_QSTR_usb_capture), MP_ROM_PTR(&camera_usb_capture_obj)},
    {MP_ROM_QSTR(MP_QSTR_onboard_capture), MP_ROM_PTR(&camera_onboard_capture_obj)},
    {MP_ROM_QSTR(MP_QSTR_load), MP_ROM_PTR(&camera_load_obj)},
    {MP_ROM_QSTR(MP_QSTR_show), MP_ROM_PTR(&camera_show_obj)},
    {MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&camera_close_obj)},
};
static MP_DEFINE_CONST_DICT(camera_globals_dict, camera_globals);
const mp_obj_module_t mp_module_camera = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&camera_globals_dict};
MP_REGISTER_MODULE(MP_QSTR_camera, mp_module_camera);
#else
void mp_camera_deinit(void) {}
#endif
