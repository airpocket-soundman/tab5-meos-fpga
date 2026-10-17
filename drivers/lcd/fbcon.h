#pragma once
#include <m5os/types.h>
/* Text console on the LCD: shows everything the console prints (the shell, kernel messages),
 * scrolling, with a blinking cursor.  For boards without a touch screen, where the GUI cannot run. */
void fbcon_start(void);

/* Full-screen programs (the filer) own the display while they run: pause stops fbcon from drawing,
 * resume repaints the whole console.  Console output keeps being collected while paused. */
void fbcon_pause(bool pause);
