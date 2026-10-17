/* Snake: steer by touching the screen - the snake turns toward where you touch (relative to its head). */
#include "../apps.h"
#include <m5os/gui.h>
#include <m5os/display.h>
#include <m5os/kmalloc.h>
#include <m5os/hal_time.h>
#include <m5os/kprintf.h>
#include "snake_logic.h"

#define CELL      8
#define STEP_MS   140u
#define HUD_Y     (SNAKE_ROWS * CELL + 2)
#define COL_HEAD  RGB565(120, 255, 120)
#define COL_BODY  RGB565(30, 170, 60)
#define COL_FOOD  RGB565(255, 60, 60)
#define COL_BG    COLOR_BLACK

static void cell(window_t w, int x, int y, uint16_t color) { window_fill_rect(w, x * CELL, y * CELL, CELL - 1, CELL - 1, color); }

static void draw_hud(window_t w, const snake_t *s)
{
    char buf[32];
    ksnprintf(buf, sizeof buf, s->dead ? "GAME OVER  score %-4u  tap to restart" : "score %-4u", s->score);
    window_draw_text(w, 2, HUD_Y, buf, COLOR_WHITE, COL_BG, 1);
}

static void draw_all(window_t w, const snake_t *s)
{
    window_clear(w, COL_BG);
    window_draw_rect(w, 0, 0, SNAKE_COLS * CELL, SNAKE_ROWS * CELL, RGB565(60, 60, 60));
    for (int i = s->len - 1; i >= 0; i--) cell(w, s->x[i], s->y[i], i == 0 ? COL_HEAD : COL_BODY);
    cell(w, s->food_x, s->food_y, COL_FOOD);
    draw_hud(w, s);
}

static void steer(snake_t *s, int tx, int ty)
{
    int dx = tx - (s->x[0] * CELL + CELL / 2), dy = ty - (s->y[0] * CELL + CELL / 2);
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ax >= ay) snake_turn(s, dx < 0 ? SNAKE_LEFT : SNAKE_RIGHT);
    else          snake_turn(s, dy < 0 ? SNAKE_UP : SNAKE_DOWN);
}

void snake_main(void *arg)
{
    (void)arg;
    int wi = window_create("Snake", 0);
    if (wi < 0) return;
    window_t w = (window_t)wi;
    snake_t *s = umalloc(sizeof *s);                           /* app-owned memory: reclaimed if we are killed */
    if (!s) { window_destroy(w); return; }
    snake_init(s, (uint32_t)hal_time_now_us());
    uint32_t next = (uint32_t)(hal_time_now_us() / 1000u) + STEP_MS;

    for (;;) {
        uint32_t now = (uint32_t)(hal_time_now_us() / 1000u);
        uint32_t wait = (int32_t)(next - now) > 0 ? next - now : 0;
        gui_event_t ev;
        int rc = gui_next_event(&ev, wait);
        if (rc == ERR_OK) {
            if (ev.type == GUI_EVT_CLOSE) break;
            if (ev.type == GUI_EVT_EXPOSE) draw_all(w, s);
            else if (ev.type == GUI_EVT_TOUCH_DOWN || ev.type == GUI_EVT_TOUCH_MOVE) {
                if (s->dead) { snake_init(s, (uint32_t)hal_time_now_us()); draw_all(w, s); next = now + STEP_MS; }
                else steer(s, ev.x, ev.y);
            }
        }
        now = (uint32_t)(hal_time_now_us() / 1000u);
        if ((int32_t)(now - next) >= 0) {
            next = now + STEP_MS;
            if (!s->dead && window_is_focused(w)) {                /* pause while another window is in front */
                int tx = s->x[s->len - 1], ty = s->y[s->len - 1];  /* the tail cell, in case it is vacated */
                snake_result_t r = snake_step(s);
                if (r == SNAKE_MOVED) { cell(w, tx, ty, COL_BG); cell(w, s->x[1], s->y[1], COL_BODY); cell(w, s->x[0], s->y[0], COL_HEAD); }
                else if (r == SNAKE_ATE) { cell(w, s->x[1], s->y[1], COL_BODY); cell(w, s->x[0], s->y[0], COL_HEAD); cell(w, s->food_x, s->food_y, COL_FOOD); draw_hud(w, s); }
                else draw_hud(w, s);
            }
        }
    }
    kfree(s);
    window_destroy(w);
}
