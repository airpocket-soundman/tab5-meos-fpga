/* Clock: digital + analog.  No real-time clock is read, so it shows time since boot (HH:MM:SS). */
#include "../apps.h"
#include <m5os/gui.h>
#include <m5os/display.h>
#include <m5os/hal_time.h>
#include <m5os/kprintf.h>
#include "clock_math.h"

#define CX   160
#define CY   108
#define R_FACE   60
#define UPDATE_MS 250u
#define COL_FACE  RGB565(90, 90, 110)
#define COL_HOUR  RGB565(255, 255, 255)
#define COL_MIN   RGB565(120, 200, 255)
#define COL_SEC   RGB565(255, 80, 80)
#define COL_BG    COLOR_BLACK

typedef struct { int h, m, s; } hands_t;

static void dot(window_t w, int pos, int radius, int size, uint16_t color)
{
    int x, y;
    clock_point(CX, CY, pos, radius, &x, &y);
    window_fill_rect(w, x - size / 2, y - size / 2, size, size, color);
}

static void draw_hand(window_t w, int pos, int len, int size, uint16_t color)
{
    for (int r = 4; r <= len; r += 3) dot(w, pos, r, size, color);
}

static void draw_face(window_t w)
{
    for (int p = 0; p < 60; p++) dot(w, p, R_FACE, (p % 5 == 0) ? 5 : 2, COL_FACE);
}

static void hands_pos(const hands_t *t, int *hp, int *mp, int *sp)
{
    *hp = (t->h % 12) * 5 + t->m / 12;
    *mp = t->m;
    *sp = t->s;
}

static void draw_hands(window_t w, const hands_t *t, bool erase)
{
    int hp, mp, sp;
    hands_pos(t, &hp, &mp, &sp);
    draw_hand(w, hp, 30, 4, erase ? COL_BG : COL_HOUR);
    draw_hand(w, mp, 44, 3, erase ? COL_BG : COL_MIN);
    draw_hand(w, sp, 52, 2, erase ? COL_BG : COL_SEC);
}

static hands_t now_hands(void)
{
    uint32_t sec = (uint32_t)(hal_time_now_us() / 1000000u);
    return (hands_t){ (int)((sec / 3600u) % 24u), (int)((sec / 60u) % 60u), (int)(sec % 60u) };
}

static void draw_digital(window_t w, const hands_t *t)
{
    char buf[12];
    ksnprintf(buf, sizeof buf, "%02d:%02d:%02d", t->h, t->m, t->s);
    window_draw_text(w, (GUI_CLIENT_W - 8 * 32) / 2, 2, buf, COLOR_WHITE, COL_BG, 4);
}

void clock_main(void *arg)
{
    (void)arg;
    int w = window_create("Clock", 0);
    if (w < 0) return;
    hands_t shown = now_hands();
    bool painted = false;

    for (;;) {
        gui_event_t ev;
        int rc = gui_next_event(&ev, UPDATE_MS);
        if (rc == ERR_OK && ev.type == GUI_EVT_CLOSE) break;
        if (rc == ERR_OK && ev.type == GUI_EVT_EXPOSE) {              /* became visible: paint everything */
            shown = now_hands();
            window_clear((window_t)w, COL_BG);
            draw_face((window_t)w);
            draw_digital((window_t)w, &shown);
            draw_hands((window_t)w, &shown, false);
            painted = true;
            continue;
        }
        if (!painted) continue;
        hands_t t = now_hands();
        if (t.s != shown.s) {                                          /* once per second: move the hands */
            draw_hands((window_t)w, &shown, true);
            draw_digital((window_t)w, &t);
            draw_hands((window_t)w, &t, false);
            shown = t;
        }
    }
    window_destroy((window_t)w);
}
