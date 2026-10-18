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

static uint16_t s_scr[ROWS][COLS];
static bool s_dirty[ROWS];
static int  s_row, s_col;
static volatile bool s_paused;
static bool s_cursor_on;
static int  s_cursor_row = -1, s_cursor_col;

static uint8_t  s_esc_state; /* 0=normal, 1=saw ESC, 2=in CSI '[', 3=in OSC ']', 4=skip 1 byte */
static char     s_csi_buf[32];
static uint8_t  s_csi_len;
static uint32_t s_utf8_cp;
static uint8_t  s_utf8_rem;

static void clear_row(int r)
{
    for (int c = 0; c < COLS; c++) s_scr[r][c] = ' ';
    s_dirty[r] = true;
}

static void newline(void)
{
    s_col = 0;
    if (s_row + 1 < ROWS) { s_row++; return; }
    for (int r = 0; r + 1 < ROWS; r++) {
        for (int c = 0; c < COLS; c++) s_scr[r][c] = s_scr[r + 1][c];
        s_dirty[r] = true;
    }
    clear_row(ROWS - 1);
}

static void parse_csi_params(const char *s, int *p0, int *p1, int def0, int def1)
{
    while (*s == '?' || *s == '>' || *s == '=') s++;
    int v0 = 0, v1 = 0;
    bool any0 = false, any1 = false;
    while (*s >= '0' && *s <= '9') {
        v0 = v0 * 10 + (*s - '0');
        any0 = true;
        s++;
    }
    if (*s == ';') {
        s++;
        while (*s >= '0' && *s <= '9') {
            v1 = v1 * 10 + (*s - '0');
            any1 = true;
            s++;
        }
    }
    *p0 = any0 ? v0 : def0;
    *p1 = any1 ? v1 : def1;
}

static void put_codepoint(uint32_t cp)
{
    if (s_col >= COLS) {
        s_col = 0;
        newline();
    }
    s_scr[s_row][s_col++] = (uint16_t)(cp <= 0xFFFFu ? cp : 0xFFFDu);
    s_dirty[s_row] = true;
}

static void feed(char ch)
{
    unsigned char u = (unsigned char)ch;
    if (s_esc_state == 1) {
        if (ch == '[') { s_esc_state = 2; s_csi_len = 0; return; }
        if (ch == ']') { s_esc_state = 3; return; }
        if (ch == '(' || ch == ')' || ch == '*' || ch == '+') { s_esc_state = 4; return; }
        if (ch == 'M') { if (s_row > 0) s_row--; s_esc_state = 0; return; }
        if (ch == 'D' || ch == 'E') { if (ch == 'E') s_col = 0; newline(); s_esc_state = 0; return; }
        if (ch == 'c') {
            for (int r = 0; r < ROWS; r++) clear_row(r);
            s_row = 0; s_col = 0; s_esc_state = 0;
            return;
        }
        s_esc_state = 0;
        return;
    }
    if (s_esc_state == 2) {
        if (u >= 0x40 && u <= 0x7E) {
            s_csi_buf[s_csi_len] = 0;
            int p0 = 0, p1 = 0;
            parse_csi_params(s_csi_buf, &p0, &p1, 0, 0);
            int n = (p0 > 0) ? p0 : 1;
            if (ch == 'J') {
                if (p0 == 2 || p0 == 3) {
                    for (int r = 0; r < ROWS; r++) clear_row(r);
                    s_row = 0; s_col = 0;
                } else if (p0 == 0) {
                    for (int c = s_col; c < COLS; c++) s_scr[s_row][c] = ' ';
                    s_dirty[s_row] = true;
                    for (int r = s_row + 1; r < ROWS; r++) clear_row(r);
                } else if (p0 == 1) {
                    for (int r = 0; r < s_row; r++) clear_row(r);
                    for (int c = 0; c <= s_col && c < COLS; c++) s_scr[s_row][c] = ' ';
                    s_dirty[s_row] = true;
                }
            } else if (ch == 'H' || ch == 'f') {
                int r = (p0 > 0) ? (p0 - 1) : 0;
                int c = (p1 > 0) ? (p1 - 1) : 0;
                if (r < 0) r = 0;
                if (r >= ROWS) r = ROWS - 1;
                if (c < 0) c = 0;
                if (c >= COLS) c = COLS - 1;
                s_row = r; s_col = c;
            } else if (ch == 'K') {
                int start_c = (p0 == 2 || p0 == 1) ? 0 : s_col;
                int end_c   = (p0 == 1) ? (s_col + 1 < COLS ? s_col + 1 : COLS) : COLS;
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
            } else if (ch == 'd') {
                s_row = n - 1;
                if (s_row < 0) s_row = 0;
                if (s_row >= ROWS) s_row = ROWS - 1;
            } else if (ch == 'P') {
                if (n > COLS - s_col) n = COLS - s_col;
                for (int c = s_col; c + n < COLS; c++) s_scr[s_row][c] = s_scr[s_row][c + n];
                for (int c = COLS - n; c < COLS; c++) s_scr[s_row][c] = ' ';
                s_dirty[s_row] = true;
            } else if (ch == '@') {
                if (n > COLS - s_col) n = COLS - s_col;
                for (int c = COLS - 1; c >= s_col + n; c--) s_scr[s_row][c] = s_scr[s_row][c - n];
                for (int c = s_col; c < s_col + n && c < COLS; c++) s_scr[s_row][c] = ' ';
                s_dirty[s_row] = true;
            } else if (ch == 'X') {
                for (int c = s_col; c < s_col + n && c < COLS; c++) s_scr[s_row][c] = ' ';
                s_dirty[s_row] = true;
            }
            s_esc_state = 0;
            return;
        }
        if (s_csi_len + 1 < sizeof(s_csi_buf)) s_csi_buf[s_csi_len++] = ch;
        return;
    }
    if (s_esc_state == 3) {
        if (u == 0x07) s_esc_state = 0;
        else if (u == 0x1B) s_esc_state = 4;
        return;
    }
    if (s_esc_state == 4) {
        s_esc_state = 0;
        return;
    }

    if (u == 0x1B) { s_utf8_rem = 0; s_esc_state = 1; return; }
    if (u >= 0x80u) {
        if ((u & 0xC0u) == 0x80u) {
            if (s_utf8_rem > 0) {
                s_utf8_cp = (s_utf8_cp << 6) | (uint32_t)(u & 0x3Fu);
                s_utf8_rem--;
                if (s_utf8_rem == 0) {
                    put_codepoint(s_utf8_cp);
                }
            }
        } else if ((u & 0xE0u) == 0xC0u) {
            s_utf8_cp  = (uint32_t)(u & 0x1Fu);
            s_utf8_rem = 1;
        } else if ((u & 0xF0u) == 0xE0u) {
            s_utf8_cp  = (uint32_t)(u & 0x0Fu);
            s_utf8_rem = 2;
        } else if ((u & 0xF8u) == 0xF0u) {
            s_utf8_cp  = (uint32_t)(u & 0x07u);
            s_utf8_rem = 3;
        } else {
            s_utf8_rem = 0;
        }
        return;
    }

    s_utf8_rem = 0;
    if (ch == '\r') { s_col = 0; return; }
    if (ch == '\n') { newline(); return; }
    if (ch == '\b') { if (s_col > 0) s_col--; return; }
    if (ch == '\t') {
        int next_tab = (s_col + 8) & ~7;
        if (next_tab >= COLS) next_tab = COLS - 1;
        s_col = next_tab;
        return;
    }
    if (u < 0x20 || u == 0x7F) return;
    put_codepoint((uint32_t)u);
}

static void draw_cursor(bool on)
{
    int r = s_row, c = s_col < COLS ? s_col : COLS - 1;
    uint16_t under = s_scr[r][c];
    display_draw_codepoints(c * CELL, r * CELL, &under, 1, on ? COL_BG : COL_FG, on ? COL_FG : COL_BG, SCALE);
    s_cursor_row = r; s_cursor_col = c; s_cursor_on = on;
}

static void flush_rows(void)
{
    for (int r = 0; r < ROWS; r++) {
        if (!s_dirty[r]) continue;
        display_draw_codepoints(0, r * CELL, s_scr[r], COLS, COL_FG, COL_BG, SCALE);
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
                uint16_t under = s_scr[s_cursor_row][s_cursor_col];
                display_draw_codepoints(s_cursor_col * CELL, s_cursor_row * CELL, &under, 1, COL_FG, COL_BG, SCALE);
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
