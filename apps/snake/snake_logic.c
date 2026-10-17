#include "snake_logic.h"

static uint32_t rnd(snake_t *s) { s->rng ^= s->rng << 13; s->rng ^= s->rng >> 17; s->rng ^= s->rng << 5; return s->rng; }

static bool on_snake(const snake_t *s, int x, int y)
{
    for (int i = 0; i < s->len; i++) if (s->x[i] == x && s->y[i] == y) return true;
    return false;
}

static void place_food(snake_t *s)
{
    for (int tries = 0; tries < 10 * SNAKE_MAX; tries++) {
        int x = (int)(rnd(s) % SNAKE_COLS), y = (int)(rnd(s) % SNAKE_ROWS);
        if (!on_snake(s, x, y)) { s->food_x = (uint8_t)x; s->food_y = (uint8_t)y; return; }
    }
    s->dead = true;                                          /* no free cell left: the board is full */
}

void snake_init(snake_t *s, uint32_t seed)
{
    *s = (snake_t){ .len = SNAKE_START_LEN, .dir = SNAKE_RIGHT, .next_dir = SNAKE_RIGHT, .rng = seed ? seed : 0x2545F491u };
    for (int i = 0; i < SNAKE_START_LEN; i++) { s->x[i] = (uint8_t)(SNAKE_COLS / 2 - i); s->y[i] = SNAKE_ROWS / 2; }
    place_food(s);
}

void snake_turn(snake_t *s, snake_dir_t d)
{
    bool reverse = (s->dir == SNAKE_UP && d == SNAKE_DOWN) || (s->dir == SNAKE_DOWN && d == SNAKE_UP) ||
                   (s->dir == SNAKE_LEFT && d == SNAKE_RIGHT) || (s->dir == SNAKE_RIGHT && d == SNAKE_LEFT);
    if (!reverse) s->next_dir = (uint8_t)d;                  /* compared with the CURRENT heading, so two quick taps cannot fold the snake */
}

snake_result_t snake_step(snake_t *s)
{
    if (s->dead) return SNAKE_DEAD;
    s->dir = s->next_dir;
    int nx = s->x[0], ny = s->y[0];
    if (s->dir == SNAKE_UP) ny--; else if (s->dir == SNAKE_DOWN) ny++; else if (s->dir == SNAKE_LEFT) nx--; else nx++;

    if (nx < 0 || ny < 0 || nx >= SNAKE_COLS || ny >= SNAKE_ROWS) { s->dead = true; return SNAKE_DEAD; }
    bool ate = (nx == s->food_x && ny == s->food_y);

    /* The tail cell is free to enter unless we are growing (then it stays occupied). */
    int check = ate ? s->len : s->len - 1;
    for (int i = 0; i < check; i++) if (s->x[i] == nx && s->y[i] == ny) { s->dead = true; return SNAKE_DEAD; }

    if (ate && s->len < SNAKE_MAX) s->len++;
    for (int i = s->len - 1; i > 0; i--) { s->x[i] = s->x[i - 1]; s->y[i] = s->y[i - 1]; }
    s->x[0] = (uint8_t)nx; s->y[0] = (uint8_t)ny;
    if (ate) { s->score++; place_food(s); return SNAKE_ATE; }
    return SNAKE_MOVED;
}
