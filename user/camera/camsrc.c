#include "camsrc.h"
#ifndef M5OS_MODULE_CAMSRC
#define M5OS_MODULE_CAMSRC 1
#endif
#ifndef M5OS_MODULE_CAMERA
#define M5OS_MODULE_CAMERA 1
#endif
#ifndef M5OS_MODULE_UVC
#define M5OS_MODULE_UVC 1
#endif
#if M5OS_BOARD_TAB5 && M5OS_MODULE_CAMSRC
#include "camera_app.h"
#include <m5os/camera.h>
#include <m5os/devreg.h>
#include <m5os/hal_time.h>
#include <m5os/kmalloc.h>
#include <m5os/task.h>
#include <string.h>
#if M5OS_USB_NATIVE_DMA && M5OS_MODULE_UVC
#include "../../drivers/usb_dma/native_uvc.h"
#endif

#define FIT_MAX_W 960 /* camera_draw_rgb565_fit() scales through a 960x540 buffer */
#define FIT_MAX_H 540
#define ONBOARD_WARMUP_FRAMES 6

struct camsrc {
    camsrc_id_t id;
    uint32_t seq, last_hw_seq;
    int warmup;
    bool mirror, vflip;   /* USB: software orientation */
    bool usb_open;        /* USB: a streaming session is open */
    int limit_w, limit_h; /* USB: reduced-decode target, see camsrc_set_limit() */
};

static camsrc_t *s_open[CAMSRC_COUNT];

bool camsrc_resolve(const char *device, camsrc_id_t *id) {
    if (!device)
        return false;
    if (!strncmp(device, "/dev/", 5))
        device += 5;
    devreg_entry_t e;
    if (!devreg_find(device, &e) || e.cls != DEVREG_CAMERA)
        return false;
    if (id)
        *id = (camsrc_id_t)(intptr_t)e.obj;
    return true;
}

const char *camsrc_name(camsrc_id_t id) {
    return id == CAMSRC_ONBOARD ? "onboard" : id == CAMSRC_USB ? "usb" : "?";
}

bool camsrc_find(const char *name, camsrc_id_t *id) {
    for (int i = 0; i < CAMSRC_COUNT; i++) {
        if (name && !strcmp(name, camsrc_name((camsrc_id_t)i))) {
            if (id)
                *id = (camsrc_id_t)i;
            return true;
        }
    }
    return false;
}

bool camsrc_native_size(camsrc_id_t id, int *width, int *height) {
    if (id == CAMSRC_ONBOARD) {
        if (!M5OS_MODULE_CAMERA)
            return false;
        if (width) *width = CAMERA_WIDTH;
        if (height) *height = CAMERA_HEIGHT;
        return true;
    }
#if M5OS_USB_NATIVE_DMA && M5OS_MODULE_UVC
    if (id == CAMSRC_USB) {
        uvc_status_t st;
        native_uvc_status(&st);
        if (!st.present || !st.configured)
            return false;
        if (width) *width = st.width;
        if (height) *height = st.height;
        return true;
    }
#endif
    return false;
}

bool camsrc_available(camsrc_id_t id) { return camsrc_native_size(id, NULL, NULL); }

int camsrc_modes(camsrc_id_t id, camsrc_mode_t *out, int max) {
    int n = 0;
    if (id == CAMSRC_ONBOARD) {
        if (max > 0 && out)
            out[0] = (camsrc_mode_t){CAMERA_WIDTH, CAMERA_HEIGHT, false, 300};
        return 1;
    }
#if M5OS_USB_NATIVE_DMA && M5OS_MODULE_UVC
    if (id == CAMSRC_USB) {
        uint16_t w, h;
        uint32_t interval;
        for (int fmt = 0; fmt < 2; fmt++) {
            for (unsigned i = 0; fmt ? native_uvc_mjpeg_mode_get(i, &w, &h, &interval)
                                     : native_uvc_mode_get(i, &w, &h, &interval); i++) {
                if (n < max && out)
                    out[n] = (camsrc_mode_t){w, h, fmt == 1, interval ? (int)(100000000u / interval) : 0};
                n++;
            }
        }
    }
#endif
    return n;
}

void camsrc_set_limit(camsrc_t *c, int max_width, int max_height) {
    if (c) {
        c->limit_w = max_width;
        c->limit_h = max_height;
    }
}

bool camsrc_set_size(camsrc_t *c, int width, int height, int fmt) {
    if (!c)
        return false;
    if (c->id == CAMSRC_ONBOARD)
        return width == CAMERA_WIDTH && height == CAMERA_HEIGHT && fmt <= 0;
#if M5OS_USB_NATIVE_DMA && M5OS_MODULE_UVC
    if (width < 1 || height < 1)
        return false;
    if (c->usb_open) {        /* the open session fixed the mode: close it, choose, reopen */
        native_uvc_stream_close();
        c->usb_open = false;
    }
    bool ok = false;
    if (fmt != 0)
        ok = native_uvc_select_mode((unsigned)width, (unsigned)height, true);
    if (!ok && fmt <= 0)
        ok = native_uvc_select_mode((unsigned)width, (unsigned)height, false);
    int w, h;
    c->usb_open = native_uvc_stream_open(&w, &h);
    return ok && c->usb_open;
#else
    return false;
#endif
}

camsrc_t *camsrc_open(camsrc_id_t id) {
    if ((unsigned)id >= CAMSRC_COUNT || s_open[id] || !camsrc_available(id))
        return NULL;
    camsrc_t *c = kmalloc(sizeof *c);
    if (!c)
        return NULL;
    memset(c, 0, sizeof *c);
    c->id = id;
    if (id == CAMSRC_ONBOARD) {
        if (!camera_app_acquire()) {
            kfree(c);
            return NULL;
        }
        if (!camera_start()) {
            camera_app_release();
            kfree(c);
            return NULL;
        }
        c->warmup = ONBOARD_WARMUP_FRAMES;
    } else {
#if M5OS_USB_NATIVE_DMA && M5OS_MODULE_UVC
        int w, h;
        if (!native_uvc_stream_open(&w, &h)) {
            kfree(c);
            return NULL;
        }
        c->usb_open = true;
        c->limit_w = 960;
        c->limit_h = 540;
#endif
    }
    s_open[id] = c;
    return c;
}

camsrc_id_t camsrc_id(const camsrc_t *c) { return c->id; }

void camsrc_set_flip(camsrc_t *c, bool mirror, bool vflip) {
    if (c->id == CAMSRC_ONBOARD)
        camera_set_mirror(mirror, vflip);
    c->mirror = mirror;
    c->vflip = vflip;
}

static bool grab_onboard(camsrc_t *c, camsrc_frame_t *out, uint32_t timeout_ms) {
    uint64_t end = hal_time_now_us() + (uint64_t)timeout_ms * 1000u;
    for (;;) {
        uint32_t hw_seq = 0;
        const uint16_t *f = camera_frame(&hw_seq);
        if (f && hw_seq != c->last_hw_seq) {
            c->last_hw_seq = hw_seq;
            if ((hw_seq & 3u) == 0)
                camera_auto_exposure_step(f);
            if (c->warmup > 0) {
                c->warmup--;
            } else {
                out->pixels = f;
                out->width = CAMERA_WIDTH;
                out->height = CAMERA_HEIGHT;
                out->mirror = c->mirror;
                out->vflip = c->vflip;
                return true;
            }
        } else {
            camera_status_t st;
            camera_get_status(&st);
            if (!st.streaming)
                return false;
            task_sleep(5);
        }
        if (hal_time_now_us() >= end)
            return false;
    }
}

static bool grab_usb(camsrc_t *c, camsrc_frame_t *out, uint32_t timeout_ms) {
#if M5OS_USB_NATIVE_DMA && M5OS_MODULE_UVC
    const uint16_t *pixels = NULL;
    int w = 0, h = 0;
    if (!c->usb_open || !native_uvc_stream_next(&pixels, &w, &h, timeout_ms, c->limit_w, c->limit_h))
        return false;
    out->pixels = pixels;
    out->width = w;
    out->height = h;
    out->mirror = c->mirror;
    out->vflip = c->vflip;
    return true;
#else
    (void)c; (void)out; (void)timeout_ms;
    return false;
#endif
}

bool camsrc_grab(camsrc_t *c, camsrc_frame_t *out, uint32_t timeout_ms) {
    if (!c || !out || timeout_ms == 0)
        return false;
    bool ok = c->id == CAMSRC_ONBOARD ? grab_onboard(c, out, timeout_ms) : grab_usb(c, out, timeout_ms);
    if (ok)
        out->seq = ++c->seq;
    return ok;
}

void camsrc_close(camsrc_t *c) {
    if (!c)
        return;
    if (c->id == CAMSRC_ONBOARD) {
        camera_stop();
        camera_app_release();
    }
#if M5OS_USB_NATIVE_DMA && M5OS_MODULE_UVC
    if (c->usb_open)
        native_uvc_stream_close();
#endif
    s_open[c->id] = NULL;
    kfree(c);
}

void camsrc_scale(const camsrc_frame_t *f, uint16_t *dst, int dw, int dh) {
    if (!f || !f->pixels || !dst || dw < 1 || dh < 1 || f->width < 1 || f->height < 1)
        return;
    if (dw == f->width && dh == f->height && !f->mirror && !f->vflip) {
        memcpy(dst, f->pixels, (size_t)dw * dh * sizeof(uint16_t));
        return;
    }
    for (int y = 0; y < dh; y++) {
        int sy = (y * f->height) / dh;
        if (f->vflip)
            sy = f->height - 1 - sy;
        const uint16_t *row = &f->pixels[(size_t)sy * f->width];
        uint16_t *out = &dst[(size_t)y * dw];
        for (int x = 0; x < dw; x++) {
            int sx = (x * f->width) / dw;
            out[x] = row[f->mirror ? f->width - 1 - sx : sx];
        }
    }
}

bool camsrc_capture(camsrc_id_t id, uint16_t *dst, int dw, int dh, uint32_t timeout_ms) {
    camsrc_t *c = camsrc_open(id);
    if (!c)
        return false;
    camsrc_frame_t f;
    bool ok = camsrc_grab(c, &f, timeout_ms);
    if (ok)
        camsrc_scale(&f, dst, dw, dh);
    camsrc_close(c);
    return ok;
}

void camsrc_draw_fit(const uint16_t *pixels, int w, int h, int ax, int ay, int aw, int ah) {
    if (!pixels || w < 1 || h < 1 || aw < 1 || ah < 1)
        return;
    int bw = aw < FIT_MAX_W ? aw : FIT_MAX_W, bh = ah < FIT_MAX_H ? ah : FIT_MAX_H;
    int dw = bw, dh = (int)(((int64_t)h * bw) / w);
    if (dh > bh) {
        dh = bh;
        dw = (int)(((int64_t)w * bh) / h);
    }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    camera_draw_rgb565_fit(ax + (aw - dw) / 2, ay + (ah - dh) / 2, dw, dh, pixels, w, h);
}
#endif
