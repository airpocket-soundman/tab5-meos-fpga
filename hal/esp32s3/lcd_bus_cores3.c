/*
 * CoreS3 LCD bus: bit-banged write-only SPI (mode 0) with a GPIO-driven D/C.
 *
 * Pins (M5GFX M5GFX.cpp, CoreS3 section): SCLK=36, MOSI=37, CS=3, and D/C =
 * GPIO35, which is *shared with MISO*.  Because we never read from the panel,
 * GPIO35 can simply stay a plain output = D/C (the original driver has to
 * switch it to MISO for reads).
 *
 * Speed: one register write sets/clears both MOSI and SCLK of a bit, so a bit
 * costs a few CPU cycles; a full 320x240 RGB565 frame (1.2 Mbit) takes tens of
 * ms.  The SPI peripheral + DMA is the obvious later optimization.
 */
#include <m5os/hal_lcd_bus.h>
#include <m5os/hal_gpio.h>
#include "soc/soc.h"
#include "soc/gpio_reg.h"
#include "esp_rom_sys.h"

#define PIN_LCD_SCLK   36
#define PIN_LCD_MOSI   37
#define PIN_LCD_DC     35
#define PIN_LCD_CS     3

/* GPIO 32..53 live in the "1" register bank. */
#define BANK1_BIT(pin)   (1u << ((pin) - 32))
#define SCLK_BIT         BANK1_BIT(PIN_LCD_SCLK)
#define MOSI_BIT         BANK1_BIT(PIN_LCD_MOSI)
#define DC_BIT           BANK1_BIT(PIN_LCD_DC)
#define CS_BIT           (1u << PIN_LCD_CS)          /* GPIO3 is in bank 0 */

void hal_lcd_bus_init(void)
{
    hal_gpio_config_output(PIN_LCD_CS, 1);
    hal_gpio_config_output(PIN_LCD_SCLK, 0);          /* SPI mode 0: idle low */
    hal_gpio_config_output(PIN_LCD_MOSI, 0);
    hal_gpio_config_output(PIN_LCD_DC, 1);
}

void hal_lcd_begin(void) { REG_WRITE(GPIO_OUT_W1TC_REG, CS_BIT); }
void hal_lcd_end(void)   { REG_WRITE(GPIO_OUT_W1TS_REG, CS_BIT); }

static inline void shift_byte(uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        if (b & (1u << i)) {
            REG_WRITE(GPIO_OUT1_W1TS_REG, MOSI_BIT);      /* data = 1 */
        } else {
            REG_WRITE(GPIO_OUT1_W1TC_REG, MOSI_BIT);      /* data = 0 */
        }
        REG_WRITE(GPIO_OUT1_W1TS_REG, SCLK_BIT);          /* rising edge: panel samples */
        REG_WRITE(GPIO_OUT1_W1TC_REG, SCLK_BIT);
    }
}

void hal_lcd_write_cmd(uint8_t cmd)
{
    REG_WRITE(GPIO_OUT1_W1TC_REG, DC_BIT);                /* D/C = 0: command */
    shift_byte(cmd);
    REG_WRITE(GPIO_OUT1_W1TS_REG, DC_BIT);                /* back to data */
}

void hal_lcd_write_data(uint8_t d)  { shift_byte(d); }

void hal_lcd_write_pixel(uint16_t c)
{
    shift_byte((uint8_t)(c >> 8));
    shift_byte((uint8_t)c);
}

void hal_lcd_write_pixels(uint16_t c, uint32_t n)
{
    while (n--) hal_lcd_write_pixel(c);
}

void hal_delay_ms(uint32_t ms)
{
    while (ms--) esp_rom_delay_us(1000);
}
