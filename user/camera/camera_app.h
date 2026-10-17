#pragma once
#include <m5os/types.h>

#define CAM_PHOTO_W  640
#define CAM_PHOTO_H  360

/* Launch the interactive full-screen camera application.
 * Captured BMP images are saved into `save_dir` (defaults to "/home" if NULL or "/").
 * If `last_saved_name` is non-NULL (size >= 24), receives the basename of the last saved photo. */
void camera_app_run(const char *save_dir, char *last_saved_name);

/* Image helper functions used by the Filer's image viewer and shell `snap`. */
bool camera_capture_bmp_file(const char *path);
bool camera_is_bmp_name(const char *name);
bool camera_load_bmp(const char *path, uint16_t **out_pixels, int *out_w, int *out_h);
void camera_draw_rgb565_fit(int dst_x, int dst_y, int dst_w, int dst_h,
                            const uint16_t *src, int src_w, int src_h);
