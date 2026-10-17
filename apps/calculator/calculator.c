/* Calculator: a touch keypad on top of calc_logic.c. */
#include "../apps.h"
#include <m5os/gui.h>
#include <m5os/display.h>
#include "calc.h"

#define KEY_COLS   4
#define KEY_ROWS   4
#define KEY_W      80
#define KEY_H      31
#define KEYS_Y     44
#define DISP_H     38
#define COL_KEY    RGB565(60, 60, 75)
#define COL_OP     RGB565(190, 110, 20)
#define COL_CLEAR  RGB565(170, 40, 40)
#define COL_PRESS  RGB565(40, 160, 70)
#define COL_DISP   RGB565(10, 20, 10)

static const char s_keys[KEY_ROWS][KEY_COLS] = {
    { '7', '8', '9', '/' },
    { '4', '5', '6', '*' },
    { '1', '2', '3', '-' },
    { 'C', '0', '=', '+' },
};

static uint16_t key_color(char k)
{
    if (k == 'C') return COL_CLEAR;
    if (k == '+' || k == '-' || k == '*' || k == '/' || k == '=') return COL_OP;
    return COL_KEY;
}

static void draw_key(window_t w, int r, int c, bool pressed)
{
    char k = s_keys[r][c];
    uint16_t bg = pressed ? COL_PRESS : key_color(k);
    int x = c * KEY_W, y = KEYS_Y + r * KEY_H;
    char label[2] = { k, 0 };
    window_fill_rect(w, x + 2, y + 2, KEY_W - 4, KEY_H - 4, bg);
    window_draw_text(w, x + KEY_W / 2 - 8, y + KEY_H / 2 - 8, label, COLOR_WHITE, bg, 2);
}

volatile uint32_t app_calc_painted;                         /* counts completed full repaints (read by the regression test) */
char app_calc_display[16];                                  /* what is on screen (read by the regression test) */

static void draw_display(window_t w, const calc_t *c)
{
    char buf[16];
    calc_display(c, buf, sizeof buf);
    for (int i = 0; i < (int)sizeof buf; i++) app_calc_display[i] = buf[i];
    int len = 0;
    while (buf[len]) len++;
    window_fill_rect(w, 2, 2, GUI_CLIENT_W - 4, DISP_H, COL_DISP);
    window_draw_text(w, GUI_CLIENT_W - 8 - len * 24, 2 + (DISP_H - 24) / 2, buf, COLOR_GREEN, COL_DISP, 3);
}

static bool hit(int x, int y, int *r, int *c)
{
    if (y < KEYS_Y || x < 0 || x >= KEY_COLS * KEY_W) return false;
    *c = x / KEY_W;
    *r = (y - KEYS_Y) / KEY_H;
    return *r >= 0 && *r < KEY_ROWS;
}

void calculator_main(void *arg)
{
    (void)arg;
    int w = window_create("Calculator", 0);
    if (w < 0) return;
    calc_t calc;
    calc_init(&calc);
    int pr = -1, pc = -1;                                             /* key currently held down */

    for (;;) {
        gui_event_t ev;
        if (gui_next_event(&ev, 1000) != ERR_OK) continue;
        if (ev.type == GUI_EVT_CLOSE) break;
        if (ev.type == GUI_EVT_EXPOSE) {
            window_clear((window_t)w, COLOR_BLACK);
            draw_display((window_t)w, &calc);
            for (int r = 0; r < KEY_ROWS; r++) for (int c = 0; c < KEY_COLS; c++) draw_key((window_t)w, r, c, false);
            pr = pc = -1;
            app_calc_painted++;
        } else if (ev.type == GUI_EVT_TOUCH_DOWN) {
            int r, c;
            if (hit(ev.x, ev.y, &r, &c)) {
                pr = r; pc = c;
                draw_key((window_t)w, r, c, true);
                calc_press(&calc, s_keys[r][c]);
                draw_display((window_t)w, &calc);
            }
        } else if (ev.type == GUI_EVT_TOUCH_UP && pr >= 0) {
            draw_key((window_t)w, pr, pc, false);
            pr = pc = -1;
        }
    }
    window_destroy((window_t)w);
}
