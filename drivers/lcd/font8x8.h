#pragma once
#include <m5os/types.h>
/* 8x8 ASCII 0x20..0x7E.  One byte per row, bit0 = leftmost pixel.
 * Public-domain "font8x8_basic" by Daniel Hepper. */
#define FONT8X8_FIRST  0x20
#define FONT8X8_LAST   0x7E
extern const uint8_t font8x8[FONT8X8_LAST - FONT8X8_FIRST + 1][8];
