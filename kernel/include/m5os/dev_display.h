#pragma once
#include <m5os/types.h>
#include <m5os/display.h>

/* /dev/display
 *   write(): text terminal inside a pixel band (default: whole screen).
 *            '\n' newline, '\t' 4 spaces; wraps to the top of the band and
 *            clears each line before reuse.
 *   ioctl(): graphics primitives below. */
#define DISPLAY_IOC_GET_INFO     0x0101   /* arg: display_info_t*   */
#define DISPLAY_IOC_CLEAR        0x0102   /* arg: uint16_t* color; also homes the terminal cursor */
#define DISPLAY_IOC_FILL_RECT    0x0103   /* arg: display_rect_t*   */
#define DISPLAY_IOC_DRAW_RECT    0x0104   /* arg: display_rect_t*   (outline) */
#define DISPLAY_IOC_DRAW_TEXT    0x0105   /* arg: display_text_t*   */
#define DISPLAY_IOC_BLIT         0x0106   /* arg: display_blit_t*   */
#define DISPLAY_IOC_SET_TERM     0x0107   /* arg: display_term_t*   */
#define DISPLAY_IOC_BACKLIGHT    0x0108   /* arg: uint8_t* 0..255   */

typedef struct { uint16_t width, height; } display_info_t;
typedef struct { int16_t x, y, w, h; uint16_t color; } display_rect_t;
typedef struct { int16_t x, y; uint16_t fg, bg; uint8_t scale; const char *text; } display_text_t;
typedef struct { int16_t x, y, w, h; const uint16_t *pixels; } display_blit_t;
typedef struct { int16_t y0, y1; uint16_t fg, bg; uint8_t scale; } display_term_t;   /* band [y0,y1) */
