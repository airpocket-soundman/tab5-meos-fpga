#include "clock_math.h"

static const int16_t s_sin[60] = { 0, 27, 53, 79, 104, 128, 150, 171, 190, 207, 222, 234, 243, 250, 255, 256, 255, 250, 243, 234, 222, 207, 190, 171, 150, 128, 104, 79, 53, 27, 0, -27, -53, -79, -104, -128, -150, -171, -190, -207, -222, -234, -243, -250, -255, -256, -255, -250, -243, -234, -222, -207, -190, -171, -150, -128, -104, -79, -53, -27 };

int clock_sin(int pos) { return s_sin[((pos % 60) + 60) % 60]; }
int clock_cos(int pos) { return clock_sin(pos + 15); }

void clock_point(int cx, int cy, int pos, int radius, int *x, int *y)
{
    *x = cx + ((clock_sin(pos) * radius) >> 8);
    *y = cy - ((clock_cos(pos) * radius) >> 8);
}
