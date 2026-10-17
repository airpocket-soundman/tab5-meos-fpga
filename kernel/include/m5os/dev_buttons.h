#pragma once
#include <m5os/types.h>

/* /dev/buttons
 * The CoreS3 has no user buttons below the screen, so M5OS defines three
 * virtual ones: the bottom BUTTONS_STRIP_H pixels of the touch panel, split in
 * thirds (like the Core2's A/B/C).
 *   read():  one byte, bit set = button currently pressed.
 *   ioctl(BUTTONS_IOC_GET_TOUCH): raw touch point. */
#define BUTTON_A   0x01
#define BUTTON_B   0x02
#define BUTTON_C   0x04
#define BUTTONS_STRIP_H   40

#define BUTTONS_IOC_GET_TOUCH   0x0201   /* arg: buttons_touch_t* */
typedef struct { uint16_t x, y; uint8_t pressed; } buttons_touch_t;
