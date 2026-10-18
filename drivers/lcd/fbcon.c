#include "fbcon.h"
#include <m5os/display.h>
#include <m5os/kprintf.h>
#include <m5os/task.h>
#include <m5os/hal_time.h>

#define SCALE     M5OS_FBCON_SCALE
#define CELL      (8 * SCALE)
#define COLS      (DISPLAY_WIDTH / CELL)
#define ROWS      (DISPLAY_HEIGHT / CELL)
#define POLL_MS   40u
#define BLINK_MS  500u
#define HISTORY   1500              /* how much already-printed output to show when starting */
#define COL_FG    RGB565(210, 255, 210)
#define COL_BG    COLOR_BLACK

static char s_scr[ROWS][COLS + 1];
static bool s_dirty[ROWS];
static int  s_row, s_col;
static volatile bool s_paused;
static bool s_cursor_on;
static int  s_cursor_row = -1, s_cursor_col;

static uint8_t s_esc_state; /* 0=normal, 1=saw ESC, 2=in CSI '[', 3=in OSC ']' */
static char    s_csi_buf[24];
static uint8_t s_csi_len;

static void clear_row(int r)
{
    for (int c = 0; c < COLS; c++) s_scr[r][c] = ' ';
    s_scr[r][COLS] = 0;
    s_dirty[r] = true;
}

static void newline(void)
{
    s_col = 0;
    if (s_row + 1 < ROWS) { s_row++; clear_row(s_row); return; }
    for (int r = 0; r + 1 < ROWS; r++) { for (int c = 0; c <= COLS; c++) s_scr[r][c] = s_scr[r + 1][c]; s_dirty[r] = true; }
    clear_row(ROWS - 1);
}

static int parse_csi_num(const char *s, int def_val)
{
    int v = 0;
    bool any = false;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        any = true;
        s++;
    }
    return (any && v > 0) ? v : def_val;
}

static void feed(char ch)
{
    unsigned char u = (unsigned char)ch;
    if (s_esc_state == 1) {
        if (ch == '[') { s_esc_state = 2; s_csi_len = 0; return; }
        if (ch == ']') { s_esc_state = 3; return; }
        s_esc_state = 0;
        return;
    }
    if (s_esc_state == 2) {
        if (u >= 0x40 && u <= 0x7E) {
            s_csi_buf[s_csi_len] = 0;
            int n = parse_csi_num(s_csi_buf, 1);
            if (ch == 'J' && s_csi_buf[0] == '2') {
                for (int r = 0; r < ROWS; r++) clear_row(r);
                s_row = 0; s_col = 0;
            } else if (ch == 'H' || ch == 'f') {
                if (s_csi_len == 0) { s_row = 0; s_col = 0; }
            } else if (ch == 'K') {
                int start_c = (s_csi_buf[0] == '2') ? 0 : s_col;
                int end_c   = (s_csi_buf[0] == '1') ? (s_col + 1 < COLS ? s_col + 1 : COLS) : COLS;
                for (int c = start_c; c < end_c; c++) s_scr[s_row][c] = ' ';
                s_dirty[s_row] = true;
            } else if (ch == 'A') {
                s_row -= n; if (s_row < 0) s_row = 0;
            } else if (ch == 'B') {
                s_row += n; if (s_row >= ROWS) s_row = ROWS - 1;
            } else if (ch == 'C') {
                s_col += n; if (s_col >= COLS) s_col = COLS - 1;
            } else if (ch == 'D') {
                s_col -= n; if (s_col < 0) s_col = 0;
            } else if (ch == 'G') {
                s_col = n - 1;
                if (s_col < 0) s_col = 0;
                if (s_col >= COLS) s_col = COLS - 1;
            }
            s_esc_state = 0;
            return;
        }
        if (s_csi_len + 1 < sizeof(s_csi_buf)) s_csi_buf[s_csi_len++] = ch;
        return;
    }
    if (s_esc_state == 3) {
        if (u == 0x07 || ch == '\\') s_esc_state = 0;
        return;
    }
    if (u == 0x1B) { s_esc_state = 1; return; }
    if (ch == '\r') { s_col = 0; return; }
    if (ch == '\n') { newline(); return; }
    if (ch == '\b') { if (s_col > 0) s_col--; return; }
    if (u < 0x20 || u > 0x7E) return;
    if (s_col >= COLS) newline();
    s_scr[s_row][s_col++] = ch;
    s_dirty[s_row] = true;
}

static void draw_cursor(bool on)
{
    int r = s_row, c = s_col < COLS ? s_col : COLS - 1;
    char under[2] = { s_scr[r][c], 0 };
    display_draw_text(c * CELL, r * CELL, under, on ? COL_BG : COL_FG, on ? COL_FG : COL_BG, SCALE);
    s_cursor_row = r; s_cursor_col = c; s_cursor_on = on;
}

static void flush_rows(void)
{
    for (int r = 0; r < ROWS; r++) {
        if (!s_dirty[r]) continue;
        display_draw_text(0, r * CELL, s_scr[r], COL_FG, COL_BG, SCALE);
        s_dirty[r] = false;
        if (r == s_cursor_row) s_cursor_on = false;         /* the row was repainted: the cursor cell is gone */
    }
}

static void fbcon_task(void *arg)
{
    (void)arg;
    for (int r = 0; r < ROWS; r++) clear_row(r);
    uint32_t end = kconsole_log_end();
    uint32_t pos = end > HISTORY ? end - HISTORY : 0;
    uint32_t last_blink = (uint32_t)(hal_time_now_us() / 1000u);
    bool blink = true;

    for (;;) {
        char buf[64];
        size_t n;
        bool moved = false;
        while ((n = kconsole_log_read(&pos, buf, sizeof buf)) > 0) { for (size_t i = 0; i < n; i++) feed(buf[i]); moved = true; }
        if (s_paused) { task_sleep(POLL_MS); continue; }
        flush_rows();

        uint32_t now = (uint32_t)(hal_time_now_us() / 1000u);
        if (moved) { blink = true; last_blink = now; }
        else if (now - last_blink >= BLINK_MS) { blink = !blink; last_blink = now; moved = true; }

        int cur_c = s_col < COLS ? s_col : COLS - 1;
        if (moved || s_cursor_row != s_row || s_cursor_col != cur_c) {
            if (s_cursor_row >= 0 && (s_cursor_row != s_row || s_cursor_col != cur_c) && s_cursor_on) {
                char under[2] = { s_scr[s_cursor_row][s_cursor_col], 0 };
                display_draw_text(s_cursor_col * CELL, s_cursor_row * CELL, under, COL_FG, COL_BG, SCALE);
            }
            draw_cursor(blink);
        }
        task_sleep(POLL_MS);
    }
}

void fbcon_pause(bool pause)
{
    if (!pause) {
        display_clear(COL_BG);
        for (int r = 0; r < ROWS; r++) s_dirty[r] = true;      /* repaint everything on the next pass */
        s_cursor_row = -1;
    }
    s_paused = pause;
}

void fbcon_start(void)
{
    task_create_stack("fbcon", fbcon_task, NULL, TASK_PRIO_DEFAULT + 1, 4096);
}
