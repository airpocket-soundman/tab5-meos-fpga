#include "uvc_app.h"
#if M5OS_BOARD_TAB5 && M5OS_USB_NATIVE_DMA && M5OS_MODULE_UVC
#include "../../drivers/lcd/fbcon.h"
#include "../../drivers/usb_dma/native_uvc.h"
#include "../camera/camera_app.h"
#if M5OS_MODULE_CAMSRC
#include "../camera/camsrc.h"
#endif
#include <m5os/hal_time.h>
#include <m5os/display.h>
#include <m5os/kmalloc.h>
#include <m5os/kprintf.h>
#include <m5os/task.h>
#include <string.h>
int uvc_cmd_main(int argc, char **argv) {
  if (argc < 2 || !strcmp(argv[1], "status")) {
    uvc_status_t v;
    native_uvc_status(&v);
    kprintf("UVC: present=%u configured=%u address=%u version=%04x YUY2=%ux%u "
            "format=%u frame=%u\n",
            v.present, v.configured, v.address, v.version, v.width, v.height,
            v.format, v.frame);
    kprintf("UVC: last frame %u bytes, waited %u us, decoded in %u us; frame ends seen %u, damaged %u\n",
            v.last_bytes, v.last_wait_us, v.last_decode_us, v.eofs, v.bad_eofs);
    kprintf("UVC: packets with error bit %u, with bad header %u; JPEG rejected: header %u, no EOI %u, data %u\n",
            v.err_flags, v.hdr_bad, v.jpeg_bad_header, v.jpeg_no_eoi, v.jpeg_bad_data);
    kprintf("UVC: packets=%u complete=%u dropped=%u errors=%u alt=%u "
            "payload=%u raw_mps=%04x\n",
            v.packets, v.frames, v.dropped, v.errors, v.alt, v.payload,
            v.raw_mps);
    return 0;
  }
  if (!strcmp(argv[1], "interval") && argc == 3) {
    uint32_t n = 0;
    for (const char *s = argv[2]; *s >= '0' && *s <= '9'; s++)
      n = n * 10 + (uint32_t)(*s - '0');
    native_uvc_set_interval(n);
    kprintf("uvc: requested frame interval %u x 100 ns (0 = default)\n", n);
    return 0;
  }
  if (!strcmp(argv[1], "modes")) {
    unsigned n = native_uvc_mode_count();
    if (!n)
      kprintf("uvc: no USB camera\n");
    for (unsigned i = 0; i < n; i++) {
      uint16_t w, h;
      uint32_t interval;
      if (native_uvc_mode_get(i, &w, &h, &interval) && interval)
        kprintf("uvc: mode %ux%u default %u.%u fps (interval %u)\n", w, h, 10000000u / interval,
                (100000000u / interval) % 10u, interval);
    }
    for (unsigned i = 0; i < native_uvc_mjpeg_mode_count(); i++) {
      uint16_t w, h;
      uint32_t interval;
      if (native_uvc_mjpeg_mode_get(i, &w, &h, &interval) && interval)
        kprintf("uvc: MJPEG %ux%u default %u.%u fps (interval %u)\n", w, h, 10000000u / interval,
                (100000000u / interval) % 10u, interval);
    }
    return n ? 0 : 1;
  }
  if (!strcmp(argv[1], "size") && (argc == 3 || argc == 4)) {
    unsigned w = 0, h = 0;
    const char *s = argv[2];
    for (; *s >= '0' && *s <= '9'; s++) w = w * 10 + (unsigned)(*s - '0');
    if (*s == 'x') s++;
    for (; *s >= '0' && *s <= '9'; s++) h = h * 10 + (unsigned)(*s - '0');
    bool want_mjpeg = argc == 4 && !strcmp(argv[3], "mjpeg");
    bool ok = native_uvc_select_mode(w, h, want_mjpeg);
    kprintf("uvc: size %ux%u %s %s\n", w, h, want_mjpeg ? "MJPEG" : "YUY2", ok ? "selected" : "not available");
    return ok ? 0 : 1;
  }
#if !M5OS_MODULE_CAMSRC
  if (!strcmp(argv[1], "live")) {
    kprintf("uvc: live needs the camsrc module\n");
    return 1;
  }
#else
  if (!strcmp(argv[1], "live")) {
    uint32_t seconds = 10;
    unsigned limit_w = 0, limit_h = 0; /* largest picture wanted: MJPEG is decoded reduced to fit it (0 = default) */
    if (argc >= 3) {
      seconds = 0;
      for (const char *s = argv[2]; *s >= '0' && *s <= '9'; s++)
        seconds = seconds * 10 + (uint32_t)(*s - '0');
    }
    if (argc == 4) {
      const char *s = argv[3];
      for (; *s >= '0' && *s <= '9'; s++) limit_w = limit_w * 10 + (unsigned)(*s - '0');
      if (*s == 'x') s++;
      for (; *s >= '0' && *s <= '9'; s++) limit_h = limit_h * 10 + (unsigned)(*s - '0');
    }
    if (seconds < 1 || seconds > 3600 || argc > 4) {
      kprintf("usage: uvc live [SECONDS 1..3600 [MAXWxMAXH]]\n");
      return 1;
    }
    camsrc_t *cam = camsrc_open(CAMSRC_USB);
    if (!cam) {
      kprintf("uvc: no USB camera\n");
      return 1;
    }
    if (limit_w && limit_h)
      camsrc_set_limit(cam, (int)limit_w, (int)limit_h);
    bool paused = fbcon_is_paused();
    fbcon_pause(true);
    task_sleep(80);
    display_clear(COLOR_BLACK);
    uint64_t end = hal_time_now_us() + (uint64_t)seconds * 1000000u;
    uint32_t frames = 0;
    while (hal_time_now_us() < end && kconsole_getc() < 0) {
      camsrc_frame_t f;
      if (!camsrc_grab(cam, &f, 5000))
        break;
      bool batch = display_begin_frame();
      camsrc_draw_fit(f.pixels, f.width, f.height, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
      if (batch)
        display_end_frame();
      frames++;
    }
    camsrc_close(cam);
    fbcon_pause(paused);
    kprintf("uvc: live showed %u frame(s)\n", frames);
    return frames ? 0 : 1;
  }
#endif
  bool preview = !strcmp(argv[1], "preview"),
       save = !strcmp(argv[1], "capture") && argc == 3;
  if (!preview && !save) {
    kprintf("usage: uvc status | preview | live [SECONDS [MAXWxMAXH]] | modes | size WxH [mjpeg] | capture PATH | interval N\n");
    return 1;
  }
  uint16_t *p = NULL;
  int w = 0, h = 0;
  if (!native_uvc_capture(&p, &w, &h, 10000)) {
    kprintf("uvc: no complete frame captured\n");
    return 1;
  }
  bool ok = true;
  if (save) {
    ok = camera_image_save_rgb565(argv[2], p, w, h);
    kprintf("uvc: %s %ux%u BMP %s\n", ok ? "saved" : "save failed", w, h,
            argv[2]);
  }
  if (preview) {
    bool paused = fbcon_is_paused();
    fbcon_pause(true);
    task_sleep(80);
    bool batch = display_begin_frame();
    display_clear(COLOR_BLACK);
    camera_draw_rgb565_fit(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, p, w, h);
    if (batch)
      display_end_frame();
    task_sleep(1500);
    fbcon_pause(paused);
  }
  kfree(p);
  return ok ? 0 : 1;
}
#else
int uvc_cmd_main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  return 1;
}
#endif
