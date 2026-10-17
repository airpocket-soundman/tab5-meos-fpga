/*
 * M5Cardputer bring-up.  Unlike the CoreS3 there is no power-management IC to program: the
 * panel is reset with GPIO33 and its backlight is GPIO38 (M5GFX drives it with PWM; here it is
 * simply on or off).  Pins are from M5GFX.cpp (Cardputer autodetect block).
 */
#include <m5os/board.h>
#include <m5os/hal_gpio.h>
#include <m5os/hal_lcd_bus.h>
#include <m5os/kprintf.h>

#define PIN_LCD_RST        33
#define PIN_LCD_BACKLIGHT  38
#define RESET_PULSE_MS     10
#define RESET_RECOVER_MS   120      /* ST7789: wait before sending commands after a hardware reset */

bool board_init(void)
{
    hal_gpio_config_output(PIN_LCD_BACKLIGHT, 0);        /* dark until the first frame is drawn */
    hal_gpio_config_output(PIN_LCD_RST, 1);
    hal_lcd_bus_init();

    hal_gpio_write(PIN_LCD_RST, 0);
    hal_delay_ms(RESET_PULSE_MS);
    hal_gpio_write(PIN_LCD_RST, 1);
    hal_delay_ms(RESET_RECOVER_MS);
    kprintf("board: M5Cardputer (ST7789 240x135, keyboard, no PSRAM)\n");
    return true;
}

void board_backlight(uint8_t level)
{
    hal_gpio_write(PIN_LCD_BACKLIGHT, level ? 1 : 0);
}
