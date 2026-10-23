#pragma once
#include <m5os/types.h>
/* Raw board HAL. Only common IO service may configure claimed external pins. */
void hal_io_gpio_input(int pin, int pull); /* 0:none,1:up,2:down */
void hal_io_gpio_output(int pin, int initial);
void hal_io_gpio_release(int pin);
