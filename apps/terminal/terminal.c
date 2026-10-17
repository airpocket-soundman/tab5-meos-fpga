/*
 * Terminal: shows what the console prints (the shell's output, kernel messages) and has buttons
 * that type common commands into the shell.  Characters are injected into the console input, so
 * it really is the shell that runs them - the same one the serial port talks to.
 */
#include "../apps.h"
#include <m5os/gui.h>
#include <m5os/display.h>
#include <m5os/kprintf.h>

#define ROWS      18
#define COLS      40
#define CELL_H    8
#define BAR_Y     (ROWS * CELL_H + 2)
#define BAR_H     22
#define BTN_COUNT 6
#define BTN_W     (GUI_CLIENT_W / BTN_COUNT)
#define POLL_MS   100u
#define HISTORY   720                               /* characters of past output to show on start */
#define COL_TEXT  RGB565(170, 255, 170)
#define COL_BG    COLOR_BLACK
#define COL_BTN   RGB565(50, 60, 90)
#define COL_DOWN  RGB565(40, 160, 70)

static char s_scr[ROWS][COLS + 1];
static bool s_dirty[ROWS];
static int  s_row, s_col;

static const struct { const char *label; const char *command; } s_buttons[BTN_COUNT] = {
    { "ps", "ps\r" }, { "mem", "mem\r" }, { "up", "uptime\r" }, { "ls", "ls\r" }, { "apps", "apps\r" }, { "help", "help\r" },
};

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

static void feed(char ch)
{
    if (ch == '\r') return;
    if (ch == '\n') { newline(); return; }
    if (ch == '\b') { if (s_col > 0) s_col--; return; }
    if ((unsigned char)ch < 0x20 || (unsigned char)ch > 0x7E) return;
    if (s_col >= COLS) newline();
    s_scr[s_row][s_col++] = ch;
    s_dirty[s_row] = true;
}

static void draw_rows(window_t w, bool all)
{
    for (int r = 0; r < ROWS; r++) {
        if (all || s_dirty[r]) { window_draw_text(w, 0, r * CELL_H, s_scr[r], COL_TEXT, COL_BG, 1); s_dirty[r] = false; }
    }
}

static void draw_button(window_t w, int i, bool down)
{
    int len = 0;
    while (s_buttons[i].label[len]) len++;
    uint16_t bg = down ? COL_DOWN : COL_BTN;
    window_fill_rect(w, i * BTN_W + 1, BAR_Y, BTN_W - 2, BAR_H, bg);
    window_draw_text(w, i * BTN_W + (BTN_W - len * 8) / 2, BAR_Y + (BAR_H - 8) / 2, s_buttons[i].label, COLOR_WHITE, bg, 1);
}

static void pump_console(uint32_t *pos)
{
    char buf[64];
    size_t n;
    while ((n = kconsole_log_read(pos, buf, sizeof buf)) > 0) for (size_t i = 0; i < n; i++) feed(buf[i]);
}

void terminal_main(void *arg)
{
    (void)arg;
    int wi = window_create("Terminal", 0);
    if (wi < 0) return;
    window_t w = (window_t)wi;

    for (int r = 0; r < ROWS; r++) clear_row(r);
    s_row = 0; s_col = 0;
    uint32_t end = kconsole_log_end();
    uint32_t pos = end > HISTORY ? end - HISTORY : 0;               /* start with a screenful of recent history */
    pump_console(&pos);
    int pressed = -1;

    for (;;) {
        gui_event_t ev;
        int rc = gui_next_event(&ev, POLL_MS);
        if (rc == ERR_OK) {
            if (ev.type == GUI_EVT_CLOSE) break;
            if (ev.type == GUI_EVT_EXPOSE) {
                window_clear(w, COL_BG);
                draw_rows(w, true);
                for (int i = 0; i < BTN_COUNT; i++) draw_button(w, i, false);
            } else if (ev.type == GUI_EVT_TOUCH_DOWN && ev.y >= BAR_Y && ev.y < BAR_Y + BAR_H) {
                int i = ev.x / BTN_W;
                if (i >= 0 && i < BTN_COUNT) { pressed = i; draw_button(w, i, true); kconsole_inject(s_buttons[i].command); }
            } else if (ev.type == GUI_EVT_TOUCH_UP && pressed >= 0) {
                draw_button(w, pressed, false);
                pressed = -1;
            }
        }
        pump_console(&pos);
        draw_rows(w, false);
    }
    window_destroy(w);
}
