#pragma once
#include <m5os/types.h>

/* The calculator's brain, separate from its screen so it can be tested.
 * Immediate execution (like a pocket calculator): 12 + 30 = shows 42; 2 + 3 * 4 = shows 20.
 * Integers only: a result beyond +-999,999,999 or a division by zero shows "Error" until C. */
#define CALC_MAX_DIGITS  9
#define CALC_LIMIT       999999999LL

typedef struct {
    int64_t acc;          /* the running value */
    int64_t entry;        /* the number being typed */
    char    op;           /* pending operator: + - * /  or 0 */
    int     digits;       /* digits typed so far (leading zeros do not count) */
    bool    typing;       /* a number is being entered */
    bool    after_equals; /* the last key was '=' */
    bool    error;
} calc_t;

void calc_init(calc_t *c);
void calc_press(calc_t *c, char key);                       /* '0'..'9' '+' '-' '*' '/' '=' 'C' */
void calc_display(const calc_t *c, char *out, size_t cap);  /* what the screen shows */
