#pragma once
#include <m5os/types.h>
bool touch_init(void);                             /* false if the controller does not answer */
int  touch_read(uint16_t *x, uint16_t *y);         /* number of touch points (0..2); x,y = first point */
