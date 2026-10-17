#pragma once
#include "../keyboard/keyboard.h"

/* USB HID boot keyboard on the Tab5's USB-A port (a polling task). */
void        usbkbd_start(void);
const char *usbkbd_type(void);
void        usbkbd_stats(keyboard_stats_t *out);
