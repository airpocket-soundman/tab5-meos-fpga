#pragma once
/* Text console on the LCD: shows everything the console prints (the shell, kernel messages),
 * scrolling, with a blinking cursor.  For boards without a touch screen, where the GUI cannot run. */
void fbcon_start(void);
