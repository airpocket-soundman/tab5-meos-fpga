/*
 * M5OS Camera application & BMP image codec for M5Stack Tab5.
 *
 * Viewfinder:
 *  - Streams live 1280x720 RGB565 frames from MIPI-CSI + ISP and renders a scaled
 *    preview (640x360 or 960x540) inside a Dragon-Quest-style TUI window.
 *  - Captures standard 16-bit RGB565 BMP files (640x360, top-down BITMAPV3INFOHEADER)
 *    that can be viewed in the M5 Filer or copied to a PC.
 */
#include "camera_app.h"
#include "../../drivers/lcd/fbcon.h"
#include "../tui/dqwin.h"
#include <m5os/camera.h>
#include <m5os/display.h>
#include <m5os/errno.h>
#include <m5os/fs.h>
#include <m5os/hal_time.h>
#include <m5os/kmalloc.h>
#include <m5os/kprintf.h>
#include <m5os/mutex.h>
#include <m5os/task.h>
#include <m5os/vfs.h>
#include <string.h>

#define HEADER_ROWS 3
#define FOOTER_ROWS 4
#define BMP_HDR_SIZE 70u
#define WRITE_CHUNK_BYTES (32u * 1024u)

static mutex_t s_image_lock = MUTEX_INIT;
static uint16_t *s_preview_buf; /* 960 * 540 * 2 bytes max in PSRAM */
#define MAX_PREVIEW_W 960
#define MAX_PREVIEW_H 540

#include "camera_bmp.inc"

static void camera_draw_rgb565_fit_locked(int dst_x, int dst_y, int dst_w, int dst_h,
                                          const uint16_t *src, int src_w, int src_h) {
    if (!src || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0)
        return;
    if (dst_w == src_w && dst_h == src_h) {
        display_draw_bitmap(dst_x, dst_y, src_w, src_h, src);
        return;
    }
    if (!s_preview_buf) {
        s_preview_buf = kmalloc_psram((size_t)MAX_PREVIEW_W * MAX_PREVIEW_H * 2u);
        if (!s_preview_buf) {
            return;
        }
    }
    if (dst_w > MAX_PREVIEW_W)
        dst_w = MAX_PREVIEW_W;
    if (dst_h > MAX_PREVIEW_H)
        dst_h = MAX_PREVIEW_H;

    for (int y = 0; y < dst_h; y++) {
        int sy = (y * src_h) / dst_h;
        const uint16_t *src_row = &src[(size_t)sy * src_w];
        uint16_t *dst_row = &s_preview_buf[(size_t)y * dst_w];
        for (int x = 0; x < dst_w; x++) {
            int sx = (x * src_w) / dst_w;
            dst_row[x] = src_row[sx];
        }
    }
    display_draw_bitmap(dst_x, dst_y, dst_w, dst_h, s_preview_buf);
}

void camera_draw_rgb565_fit(int x, int y, int w, int h, const uint16_t *src, int sw, int sh) {
    if (!mutex_trylock(&s_image_lock))
        return;
    camera_draw_rgb565_fit_locked(x, y, w, h, src, sw, sh);
    mutex_unlock(&s_image_lock);
}

static void extract_scaled_frame(const uint16_t *src1280, uint16_t *dst, int dst_w, int dst_h,
                                 bool mirror, bool vflip) {
    for (int y = 0; y < dst_h; y++) {
        int sy = (y * CAMERA_HEIGHT) / dst_h;
        if (vflip)
            sy = (CAMERA_HEIGHT - 1) - sy;
        const uint16_t *src_row = &src1280[(size_t)sy * CAMERA_WIDTH];
        uint16_t *dst_row = &dst[(size_t)y * dst_w];
        if (!mirror) {
            if (dst_w == 640) {
                for (int x = 0; x < 640; x++)
                    dst_row[x] = src_row[x * 2];
            } else {
                for (int x = 0; x < dst_w; x++)
                    dst_row[x] = src_row[(x * CAMERA_WIDTH) / dst_w];
            }
        } else {
            if (dst_w == 640) {
                for (int x = 0; x < 640; x++)
                    dst_row[x] = src_row[(CAMERA_WIDTH - 1) - x * 2];
            } else {
                for (int x = 0; x < dst_w; x++)
                    dst_row[x] = src_row[(CAMERA_WIDTH - 1) - (x * CAMERA_WIDTH) / dst_w];
            }
        }
    }
}

static int s_next_photo_idx = 1;

static bool next_photo_path(const char *dir, char *out_path, size_t out_len, char *out_name,
                            size_t name_len) {
    const char *base_dir = (dir && dir[0] && !(dir[0] == '/' && dir[1] == 0)) ? dir : "/home";
    vfs_info_t st;
    int rc = vfs_stat(base_dir, &st);
    if (rc == ERR_NOENT) {
        if (vfs_mkdir(base_dir) < 0)
            return false;
    } else if (rc != ERR_OK || st.type != FS_TYPE_DIR)
        return false;
    for (int tries = 0; tries < 9999; tries++) {
        int idx = s_next_photo_idx++;
        if (s_next_photo_idx > 9999)
            s_next_photo_idx = 1;
        if (name_len < 13)
            return false;
        ksnprintf(out_name, name_len, "cam_%04d.bmp", idx);
        if (strlen(base_dir) + strlen(out_name) + 2 > out_len)
            return false;
        ksnprintf(out_path, out_len, "%s/%s", base_dir, out_name);
        rc = vfs_stat(out_path, &st);
        if (rc == ERR_NOENT)
            return true;
        if (rc != ERR_OK)
            return false;
    }
    return false;
}

static void padded_line(int cx, int cy, const char *s) {
    char line[200];
    int n = 0, w = dq_cols() - cx - 2;
    while (s[n] && n < w && n < (int)sizeof line - 1) {
        line[n] = s[n];
        n++;
    }
    while (n < w && n < (int)sizeof line - 1)
        line[n++] = ' ';
    line[n] = 0;
    dq_text(cx, cy, line);
}

static void camera_app_run_locked(const char *save_dir, char *last_saved_name) {
    if (last_saved_name)
        last_saved_name[0] = 0;

    if (!s_preview_buf) {
        s_preview_buf = kmalloc_psram((size_t)MAX_PREVIEW_W * MAX_PREVIEW_H * 2u);
        if (!s_preview_buf) {
            dq_message("Camera error:", "Out of PSRAM for preview buffer.");
            return;
        }
    }

    int cols = dq_cols(), rows = dq_rows();
    int fy = rows - FOOTER_ROWS;
    int mid_rows = rows - HEADER_ROWS - FOOTER_ROWS;

    dq_clear();
    dq_window(0, 0, cols, HEADER_ROWS);
    dq_text(2, 1, "MeOS CAMERA");
    padded_line(14, 1, "Initializing SC202CS MIPI-CSI + ISP...");
    dq_window(0, HEADER_ROWS, cols, mid_rows);
    dq_window(0, fy, cols, FOOTER_ROWS);
    dq_text(
        2, fy + 1,
        "Space/Enter/S:Capture  Z:Size  M:Mirror  I:Flip  A:AutoExp  Up/Dn:Exp  L/R:Gain  Q:Quit");

    if (!camera_start()) {
        camera_status_t state;
        camera_get_status(&state);
        dq_message("Camera unavailable",
                   state.message[0] ? state.message : "No camera stream available.");
        return;
    }

    const char *target_dir =
        (save_dir && save_dir[0] && !(save_dir[0] == '/' && save_dir[1] == 0)) ? save_dir : "/home";
    bool large_view = false;
    bool full_redraw = true;
    char status_msg[96];
    ksnprintf(status_msg, sizeof status_msg, "Ready (saving to %s/cam_XXXX.bmp)", target_dir);

    uint32_t last_seq = 0;
    uint32_t fps_frames = 0, fps_val = 0;
    uint64_t fps_t0 = hal_time_now_us();

    for (;;) {
        if (full_redraw) {
            dq_window(0, 0, cols, HEADER_ROWS);
            dq_text(2, 1, "MeOS CAMERA");
            dq_window(0, HEADER_ROWS, cols, mid_rows);
            dq_window(0, fy, cols, FOOTER_ROWS);
            dq_text(2, fy + 1,
                    "Space/Enter/S:Capture  Z:Size  M:Mirror  I:Flip  A:AutoExp  Up/Dn:Exp  "
                    "L/R:Gain  Q:Quit");
            padded_line(2, fy + 2, status_msg);
            full_redraw = false;
        }

        uint32_t seq = 0;
        const uint16_t *frame = camera_frame(&seq);
        if (frame && seq != last_seq) {
            last_seq = seq;
            fps_frames++;
            uint64_t now = hal_time_now_us();
            if (now - fps_t0 >= 1000000ULL) {
                fps_val = (uint32_t)((fps_frames * 1000000ULL) / (now - fps_t0));
                fps_frames = 0;
                fps_t0 = now;
            }

            if ((seq & 3u) == 0) {
                camera_auto_exposure_step(frame);
            }

            camera_status_t st;
            camera_get_status(&st);

            int vw = large_view ? 960 : 640;
            int vh = large_view ? 540 : 360;
            int vx = (DISPLAY_WIDTH - vw) / 2;
            int vy = HEADER_ROWS * 16 + (mid_rows * 16 - vh) / 2;

            extract_scaled_frame(frame, s_preview_buf, vw, vh, st.mirror, st.vflip);
            display_draw_rect(vx - 2, vy - 2, vw + 4, vh + 4, COLOR_WHITE);
            display_draw_bitmap(vx, vy, vw, vh, s_preview_buf);

            static const uint8_t gain_mult[] = {1, 1, 2, 2, 4, 5, 8, 11, 16, 22, 32, 45};
            uint32_t gm = st.gain_idx < sizeof gain_mult ? gain_mult[st.gain_idx] : 32;
            char hdr_info[120];
            ksnprintf(
                hdr_info, sizeof hdr_info,
                "SC202CS 1280x720  %2u fps  AE:%s  Exp:%4u  Gain:%2ux  Mir:%s Flip:%s  [%dx%d]",
                fps_val, st.auto_exp ? "ON " : "MAN", st.exposure, gm, st.mirror ? "Y" : "N",
                st.vflip ? "Y" : "N", vw, vh);
            padded_line(14, 1, hdr_info);
        } else {
            camera_status_t state;
            camera_get_status(&state);
            if (!state.streaming) {
                dq_message("Camera stream stopped", state.message);
                break;
            }
            task_sleep(5);
        }

        int k = dq_pollkey();
        if (k == DQK_NONE)
            continue;

        camera_status_t st;
        camera_get_status(&st);

        if (k == 'q' || k == 'Q' || k == DQK_ESC) {
            break;
        } else if (k == 'z' || k == 'Z') {
            large_view = !large_view;
            full_redraw = true;
        } else if (k == 'm' || k == 'M') {
            camera_set_mirror(!st.mirror, st.vflip);
            ksnprintf(status_msg, sizeof status_msg, "Mirror: %s", !st.mirror ? "ON" : "OFF");
            padded_line(2, fy + 2, status_msg);
        } else if (k == 'i' || k == 'I') {
            camera_set_mirror(st.mirror, !st.vflip);
            ksnprintf(status_msg, sizeof status_msg, "Vertical flip: %s", !st.vflip ? "ON" : "OFF");
            padded_line(2, fy + 2, status_msg);
        } else if (k == 'a' || k == 'A') {
            camera_set_auto_exposure(!st.auto_exp);
            ksnprintf(status_msg, sizeof status_msg, "Auto Exposure: %s",
                      !st.auto_exp ? "ON" : "MANUAL");
            padded_line(2, fy + 2, status_msg);
        } else if (k == DQK_UP || k == '+' || k == '=') {
            camera_set_auto_exposure(false);
            camera_set_exposure(st.exposure + 64u);
            camera_get_status(&st);
            ksnprintf(status_msg, sizeof status_msg, "Manual Exposure: %u", st.exposure);
            padded_line(2, fy + 2, status_msg);
        } else if (k == DQK_DOWN || k == '-' || k == '_') {
            camera_set_auto_exposure(false);
            camera_set_exposure(st.exposure > 72u ? st.exposure - 64u : 8u);
            camera_get_status(&st);
            ksnprintf(status_msg, sizeof status_msg, "Manual Exposure: %u", st.exposure);
            padded_line(2, fy + 2, status_msg);
        } else if (k == DQK_RIGHT || k == ']') {
            camera_set_auto_exposure(false);
            camera_set_gain_index(st.gain_idx + 1);
            camera_get_status(&st);
            ksnprintf(status_msg, sizeof status_msg, "Manual Gain index: %u", st.gain_idx);
            padded_line(2, fy + 2, status_msg);
        } else if (k == DQK_LEFT || k == '[') {
            camera_set_auto_exposure(false);
            camera_set_gain_index(st.gain_idx > 0 ? st.gain_idx - 1 : 0);
            camera_get_status(&st);
            ksnprintf(status_msg, sizeof status_msg, "Manual Gain index: %u", st.gain_idx);
            padded_line(2, fy + 2, status_msg);
        } else if (k == ' ' || k == DQK_ENTER || k == 's' || k == 'S' || k == 'c' || k == 'C') {
            const uint16_t *snap = camera_frame(NULL);
            if (!snap) {
                ksnprintf(status_msg, sizeof status_msg, "No camera frame available yet!");
                padded_line(2, fy + 2, status_msg);
                continue;
            }
            char path[VFS_PATH_MAX], fname[32];
            if (!next_photo_path(target_dir, path, sizeof path, fname, sizeof fname)) {
                ksnprintf(status_msg, sizeof status_msg, "Cannot generate photo path");
                padded_line(2, fy + 2, status_msg);
                continue;
            }

            int vw = large_view ? 960 : 640;
            int vh = large_view ? 540 : 360;
            int vx = (DISPLAY_WIDTH - vw) / 2;
            int vy = HEADER_ROWS * 16 + (mid_rows * 16 - vh) / 2;
            display_draw_rect(vx - 3, vy - 3, vw + 6, vh + 6, COLOR_YELLOW);

            extract_scaled_frame(snap, s_preview_buf, CAM_PHOTO_W, CAM_PHOTO_H, st.mirror,
                                 st.vflip);
            if (camera_image_save_rgb565(path, s_preview_buf, CAM_PHOTO_W, CAM_PHOTO_H)) {
                ksnprintf(status_msg, sizeof status_msg,
                          "Saved %s (%dx%d BMP, %u KB) - open in Filer to view!", path, CAM_PHOTO_W,
                          CAM_PHOTO_H,
                          (unsigned)((BMP_HDR_SIZE + CAM_PHOTO_W * CAM_PHOTO_H * 2u) / 1024u));
                if (last_saved_name) {
                    int i = 0;
                    for (; fname[i] && i < 23; i++)
                        last_saved_name[i] = fname[i];
                    last_saved_name[i] = 0;
                }
            } else {
                ksnprintf(status_msg, sizeof status_msg, "Failed to save %s (disk full?)", path);
            }
            padded_line(2, fy + 2, status_msg);
            full_redraw = true;
        }
    }

    camera_stop();
}

void camera_app_run(const char *dir, char *last) {
    if (!mutex_trylock(&s_image_lock)) {
        dq_message("Camera busy", "An image operation is already active.");
        return;
    }
    camera_app_run_locked(dir, last);
    camera_deinit();
    kfree(s_preview_buf);
    s_preview_buf = NULL;
    mutex_unlock(&s_image_lock);
}

static bool camera_capture_bmp_file_locked(const char *path) {
    if (!path || !path[0])
        return false;
    if (!s_preview_buf) {
        s_preview_buf = kmalloc_psram((size_t)MAX_PREVIEW_W * MAX_PREVIEW_H * 2u);
        if (!s_preview_buf)
            return false;
    }
    if (!camera_start())
        return false;

    uint32_t start_seq = 0;
    camera_frame(&start_seq);
    const uint16_t *frame = NULL;
    /* Let a few frames arrive so auto-exposure can adjust before saving */
    for (int wait = 0; wait < 60; wait++) {
        uint32_t seq = 0;
        const uint16_t *f = camera_frame(&seq);
        if (f && seq != start_seq) {
            frame = f;
            camera_auto_exposure_step(frame);
            if (seq - start_seq >= 6u)
                break;
        }
        task_sleep(20);
    }
    if (!frame) {
        camera_stop();
        return false;
    }
    camera_status_t st;
    camera_get_status(&st);
    extract_scaled_frame(frame, s_preview_buf, CAM_PHOTO_W, CAM_PHOTO_H, st.mirror, st.vflip);
    bool ok = camera_image_save_rgb565(path, s_preview_buf, CAM_PHOTO_W, CAM_PHOTO_H);
    camera_stop();
    return ok;
}

bool camera_capture_bmp_file(const char *path) {
    if (!mutex_trylock(&s_image_lock))
        return false;
    bool ok = camera_capture_bmp_file_locked(path);
    camera_deinit();
    kfree(s_preview_buf);
    s_preview_buf = NULL;
    mutex_unlock(&s_image_lock);
    return ok;
}

/* Built-in app entry point for `run camera` */
void camera_main(void *arg) {
    (void)arg;
    fbcon_pause(true);
    task_sleep(100);
    dq_clear();
    camera_app_run("/home", NULL);
    dq_clear();
    fbcon_pause(false);
}
