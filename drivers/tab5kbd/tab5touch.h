#pragma once
#include <m5os/types.h>
/* Tab5 touch panel: GT911 (ILI9881C) and ST7123-compatible (ST7123/ST7121), polled over system I2C.
 * Coordinates come out in the logical landscape frame of the display (0..1279, 0..719). */
bool tab5_touch_init(void);                      /* false: no supported touch controller on the bus */
bool tab5_touch_read(int *x, int *y);            /* true while a finger is down; x/y are then valid */
void tab5_touch_set_map(int map);                /* raw -> logical mapping 0..3 (bring-up aid, see tab5touch.c) */
int  tab5_touch_map(void);
void tab5_touch_stats(uint32_t *frames, uint32_t *errors, int *raw_x, int *raw_y, int *max_x, int *max_y);
