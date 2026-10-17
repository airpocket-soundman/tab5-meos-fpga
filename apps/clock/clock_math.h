#pragma once
#include <m5os/types.h>
/* Hand geometry on a 60-position dial (position 0 = 12 o'clock, clockwise), integer only. */
int  clock_sin(int pos);                    /* 256 * sin(pos * 6 degrees) */
int  clock_cos(int pos);
void clock_point(int cx, int cy, int pos, int radius, int *x, int *y);   /* point `radius` from the centre toward `pos` */
