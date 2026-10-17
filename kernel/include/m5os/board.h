#pragma once
#include <m5os/types.h>

/* Board-level bring-up (power ICs, resets).  CoreS3 implementation lives in
 * hal/esp32s3/board_cores3.c. */
bool board_init(void);                       /* false if a power IC does not answer */
void board_backlight(uint8_t level);         /* 0 = off .. 255 = full */
