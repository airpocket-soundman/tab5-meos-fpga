#pragma once
#include <m5os/types.h>
/* Cardputer keyboard.  keyboard_start() detects which of the two hardware versions is fitted,
 * starts a task that scans it, and feeds what is typed to the console input (kconsole_inject),
 * so the shell reads it exactly like characters arriving over the serial port. */
void        keyboard_start(void);
const char *keyboard_type(void);       /* "matrix" (original), "TCA8418" (Cardputer-ADV) or "none" */

/* Diagnostics for bring-up: how many raw press events were seen, the last key position, and the last
 * byte sent to the console. */
typedef struct { uint32_t presses; int last_x, last_y; int last_char; uint32_t i2c_errors; } keyboard_stats_t;
void keyboard_stats(keyboard_stats_t *out);
