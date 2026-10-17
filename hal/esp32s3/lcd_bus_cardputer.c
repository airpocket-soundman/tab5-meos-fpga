/*
 * Cardputer LCD bus: bit-banged write-only SPI (mode 0) to the ST7789.
 * Pins (M5GFX.cpp, Cardputer autodetect block): MOSI=35, SCLK=36, D/C=34, CS=37, RST=33
 * (reset and backlight are handled in board_cardputer.c).  All of them are GPIO 32..53,
 * i.e. the "1" register bank, and nothing else shares them (the SD card has its own pins).
 */
#include <m5os/hal_lcd_bus.h>
#include <m5os/hal_gpio.h>
#include "soc/soc.h"
#include "soc/gpio_reg.h"
#include "esp_rom_sys.h"

#define PIN_LCD_MOSI   35
#define PIN_LCD_SCLK   36
#define PIN_LCD_DC     34
#define PIN_LCD_CS     37

#define BANK1_BIT(pin)   (1u << ((pin) - 32))
#define MOSI_BIT         BANK1_BIT(PIN_LCD_MOSI)
#define SCLK_BIT         BANK1_BIT(PIN_LCD_SCLK)
#define DC_BIT           BANK1_BIT(PIN_LCD_DC)
#define CS_BIT           BANK1_BIT(PIN_LCD_CS)

void hal_lcd_bus_init(void)
{
    hal_gpio_config_output(PIN_LCD_CS, 1);
    hal_gpio_config_output(PIN_LCD_SCLK, 0);          /* SPI mode 0: idle low */
    hal_gpio_config_output(PIN_LCD_MOSI, 0);
    hal_gpio_config_output(PIN_LCD_DC, 1);
}

void hal_lcd_begin(void) { REG_WRITE(GPIO_OUT1_W1TC_REG, CS_BIT); }
void hal_lcd_end(void)   { REG_WRITE(GPIO_OUT1_W1TS_REG, CS_BIT); }

static inline void shift_byte(uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        if (b & (1u << i)) REG_WRITE(GPIO_OUT1_W1TS_REG, MOSI_BIT);
        else               REG_WRITE(GPIO_OUT1_W1TC_REG, MOSI_BIT);
        REG_WRITE(GPIO_OUT1_W1TS_REG, SCLK_BIT);          /* rising edge: the panel samples */
        REG_WRITE(GPIO_OUT1_W1TC_REG, SCLK_BIT);
    }
}

void hal_lcd_write_cmd(uint8_t cmd)
{
    REG_WRITE(GPIO_OUT1_W1TC_REG, DC_BIT);                /* D/C = 0: command */
    shift_byte(cmd);
    REG_WRITE(GPIO_OUT1_W1TS_REG, DC_BIT);
}

void hal_lcd_write_data(uint8_t d) { shift_byte(d); }
void hal_lcd_write_pixel(uint16_t c) { shift_byte((uint8_t)(c >> 8)); shift_byte((uint8_t)c); }
void hal_lcd_write_pixels(uint16_t c, uint32_t n) { while (n--) hal_lcd_write_pixel(c); }
void hal_delay_ms(uint32_t ms) { while (ms--) esp_rom_delay_us(1000); }
