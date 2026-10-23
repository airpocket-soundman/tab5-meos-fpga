#pragma once
#include <m5os/types.h>

#include <m5os/board_config.h>
#define DISPLAY_WIDTH    M5OS_DISPLAY_W
#define DISPLAY_HEIGHT   M5OS_DISPLAY_H

#define RGB565(r, g, b)  ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define COLOR_BLACK      RGB565(0, 0, 0)
#define COLOR_WHITE      RGB565(255, 255, 255)
#define COLOR_RED        RGB565(255, 0, 0)
#define COLOR_GREEN      RGB565(0, 255, 0)
#define COLOR_BLUE       RGB565(0, 0, 255)
#define COLOR_YELLOW     RGB565(255, 255, 0)
#define COLOR_CYAN       RGB565(0, 255, 255)
#define COLOR_GRAY       RGB565(128, 128, 128)

/* Drawing API. SPI boards stream to the panel. Tab5 draws through cached DPI
 * framebuffers and serializes drawing calls with a MeOS mutex. */
bool display_init(void);
void display_clear(uint16_t color);
void display_fill_rect(int x, int y, int w, int h, uint16_t color);
void display_draw_rect(int x, int y, int w, int h, uint16_t color);   /* outline */
void display_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, int scale);
void display_draw_text(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale);
void display_draw_codepoints(int x, int y, const uint16_t *cps, int count, uint16_t fg, uint16_t bg, int scale);
void display_draw_bitmap(int x, int y, int w, int h, const uint16_t *rgb565);
void display_draw_line(int x0, int y0, int x1, int y1, uint16_t color);                          /* Tab5 */
void display_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color);   /* Tab5 */
#define DISPLAY_BUFFER_BYTES ((size_t)DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t))
void display_set_target(uint16_t *buf);     /* Tab5: this task draws into an off-screen buffer of DISPLAY_BUFFER_BYTES (NULL: the screen again) */
void display_present(int x, int y, int w, int h);   /* Tab5: copy a rectangle of that buffer to the screen */
void display_read_bitmap(int x, int y, int w, int h, uint16_t *rgb565_out);   /* Tab5: copy of the screen, to put back with display_draw_bitmap */
void display_draw_bitmap_scaled(int x, int y, int w, int h, const uint16_t *rgb565, int scale);

/* Tab5: serialize a batch into an internal DPI back buffer, then publish it at
 * a frame boundary. begin=false means no back buffer/another offscreen target;
 * use normal drawing in that case. A successful begin MUST pair with end in
 * the same task, without calling set_target/present inside the batch. */
bool display_begin_frame(void);
bool display_end_frame(void);

typedef struct {
    uint32_t refreshes, batches, submitted, retired, queue_errors, timeouts, orphaned;
    uint32_t scanout_address;
    uint32_t last_copy_us, last_wait_us, last_batch_us, max_batch_us, active_us;
    uint8_t framebuffer_count;
    bool back_available, batch_active, pending;
} display_status_t;
/* Tab5: non-blocking snapshot; safe while another task owns a render batch. */
void display_get_status(display_status_t *out);
