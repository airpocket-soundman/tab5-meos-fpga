#pragma once
#include <m5os/types.h>

/* One interface over every camera: the built-in SC202CS and a USB (UVC) camera.
 *
 *   camsrc_t *c = camsrc_open(CAMSRC_USB);
 *   camsrc_frame_t f;
 *   while (camsrc_grab(c, &f, 2000)) camsrc_scale(&f, buf, 320, 180);
 *   camsrc_close(c);
 *
 * A frame is RGB565 and is borrowed: it stays valid until the next camsrc_grab() or camsrc_close() on the same
 * handle.  Native sizes differ (1280x720 built-in, whatever the USB camera negotiated), so consumers scale. */
typedef enum { CAMSRC_ONBOARD = 0, CAMSRC_USB = 1, CAMSRC_COUNT } camsrc_id_t;

typedef struct {
    const uint16_t *pixels;
    int width, height;
    uint32_t seq;           /* 1.. per handle, counts delivered frames */
    bool mirror, vflip;     /* orientation to apply when scaling (see camsrc_set_flip) */
} camsrc_frame_t;

typedef struct camsrc camsrc_t;

/* Cameras are devices of the registry (m5os/devreg.h): "camera0" is the built-in one, "camera1" a USB camera.
 * Both "camera1" and "/dev/camera1" are accepted.  False when no camera of that name is registered. */
bool camsrc_resolve(const char *device, camsrc_id_t *id);

const char *camsrc_name(camsrc_id_t id);                      /* "onboard", "usb" */
bool camsrc_find(const char *name, camsrc_id_t *id);          /* name -> id */
bool camsrc_available(camsrc_id_t id);                        /* USB: enumerated and configured; built-in: always on the Tab5 */
bool camsrc_native_size(camsrc_id_t id, int *width, int *height);   /* false when the camera is absent */

typedef struct {
    int width, height;
    bool mjpeg;      /* USB: compressed Motion-JPEG (decoded in software); otherwise raw YUY2 / the sensor pipeline */
    int fps10;       /* advertised frame rate x 10 */
} camsrc_mode_t;

/* Frame modes a camera offers (fills at most max entries, returns the count).  The built-in camera has one. */
int camsrc_modes(camsrc_id_t id, camsrc_mode_t *out, int max);
/* Choose the native mode of an open camera; takes effect at the next grab.  fmt: -1 = MJPEG if offered, else YUY2,
 * 0 = YUY2 only, 1 = MJPEG only. */
bool camsrc_set_size(camsrc_t *c, int width, int height, int fmt);
/* Largest picture the consumer wants: an MJPEG frame is decoded reduced by half, quarter or eighth as far as this allows,
 * which makes decoding much faster.  0 x 0 = full size.  Default 960 x 540 (what the LCD fit shows). */
void camsrc_set_limit(camsrc_t *c, int max_width, int max_height);

/* NULL when the camera is absent or busy (another task, or the shell camera app, has it). */
camsrc_t *camsrc_open(camsrc_id_t id);
/* Waits for the next frame.  Built-in: the first few frames after opening are skipped so auto-exposure settles. */
bool camsrc_grab(camsrc_t *c, camsrc_frame_t *out, uint32_t timeout_ms);
/* Built-in: handled by the sensor pipeline.  USB: applied in software when scaling. */
void camsrc_set_flip(camsrc_t *c, bool mirror, bool vflip);
camsrc_id_t camsrc_id(const camsrc_t *c);
void camsrc_close(camsrc_t *c);

/* Nearest-neighbour scale of a frame into dst (dw x dh RGB565), honouring the frame's orientation. */
void camsrc_scale(const camsrc_frame_t *f, uint16_t *dst, int dw, int dh);
/* Open, grab one frame, scale into dst, close. */
bool camsrc_capture(camsrc_id_t id, uint16_t *dst, int dw, int dh, uint32_t timeout_ms);

/* Draw RGB565 pixels on the LCD, keeping the aspect ratio, centred in the area (at most 960x540). */
void camsrc_draw_fit(const uint16_t *pixels, int w, int h, int ax, int ay, int aw, int ah);
