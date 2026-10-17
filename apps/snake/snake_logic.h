#pragma once
#include <m5os/types.h>

/* Snake rules, separate from the screen so they can be tested.  The board is
 * SNAKE_COLS x SNAKE_ROWS cells; the head is element 0. */
#define SNAKE_COLS  40
#define SNAKE_ROWS  20
#define SNAKE_MAX   (SNAKE_COLS * SNAKE_ROWS)
#define SNAKE_START_LEN 3

typedef enum { SNAKE_UP, SNAKE_DOWN, SNAKE_LEFT, SNAKE_RIGHT } snake_dir_t;
typedef enum { SNAKE_MOVED, SNAKE_ATE, SNAKE_DEAD } snake_result_t;

typedef struct {
    uint8_t  x[SNAKE_MAX], y[SNAKE_MAX];
    uint16_t len;
    uint8_t  dir, next_dir;
    uint8_t  food_x, food_y;
    uint16_t score;
    bool     dead;
    uint32_t rng;
} snake_t;

void           snake_init(snake_t *s, uint32_t seed);
void           snake_turn(snake_t *s, snake_dir_t d);       /* ignored if it would reverse into the neck */
snake_result_t snake_step(snake_t *s);                       /* advance one cell */
