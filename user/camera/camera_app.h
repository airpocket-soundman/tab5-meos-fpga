#pragma once
#include <m5os/types.h>

#define CAM_PHOTO_W 640
#define CAM_PHOTO_H 360

/* Launch the interactive full-screen camera application.
 * Captured BMP images are saved into `save_dir` (defaults to "/home" if NULL or "/").
 * If `last_saved_name` is non-NULL (size >= 24), receives the basename of the last saved photo. */
void camera_app_run(const char *save_dir, char *last_saved_name);

/* Used by shell `snap`.  (Loading and viewing images is user/viewer/imgview.) */
bool camera_capture_bmp_file(const char *path);
/* Exclusive use of the built-in camera (camsrc.c): acquire before camera_start(), release after camera_stop()
 * (release also calls camera_deinit()). */
bool camera_app_acquire(void);
void camera_app_release(void);
/* One built-in camera frame scaled to w x h (each 1..1280 / 1..720) into the caller's RGB565 buffer. */
bool camera_capture_rgb565(uint16_t *dst, int w, int h);
/* Standard top-down RGB565 BMP, padded rows; reports write/close failures. */
bool camera_image_save_rgb565(const char *path, const uint16_t *pixels, int w, int h);
bool camera_is_bmp_name(const char *name);
bool camera_load_bmp(const char *path, uint16_t **out_pixels, int *out_w, int *out_h);
void camera_draw_rgb565_fit(int dst_x, int dst_y, int dst_w, int dst_h, const uint16_t *src,
                            int src_w, int src_h);

/* Decode a saved BMP and inspect RGB565 pixels. A uniform image is still a
 * valid BMP; nonuniform is evidence of variation, not proof of image quality.
 * fnv1a hashes decoded pixels in low-byte/high-byte order, independent of BMP
 * row direction or source pixel format. Output is zeroed on failure. */
typedef struct {
    uint32_t width, height, pixel_count, nonzero_pixels;
    uint16_t minimum, maximum;
    uint32_t fnv1a;
    bool nonuniform;
} camera_bmp_stats_t;
bool camera_verify_bmp_file(const char *path, camera_bmp_stats_t *out);
