/*
 * Cardputer SD slot on its own SPI pins (M5Unified.inl _pin_table_sd): SCLK=40, MOSI=14,
 * MISO=39, CS=12.  Bit-banged like the rest.  Nothing is shared with the LCD, so the
 * begin/end hooks only have to make sure the pins are configured.
 */
#include <m5os/hal_sdspi.h>
#include <m5os/hal_gpio.h>
#include "soc/soc.h"
#include "soc/gpio_reg.h"
#include "esp_rom_sys.h"

#define PIN_SCLK  40
#define PIN_MOSI  14
#define PIN_MISO  39
#define PIN_CS    12
#define SLOW_HALF_PERIOD_US  2          /* ~250 kHz: inside the 100-400 kHz init window */

/* GPIO 0..31 are in the "0" register bank, 32..53 in the "1" bank.  Inline functions (not macros)
 * so the compiler does not see the unused bank's shift with a constant out-of-range count. */
static inline void PIN_SET(int p) { if (p < 32) REG_WRITE(GPIO_OUT_W1TS_REG, 1u << p); else REG_WRITE(GPIO_OUT1_W1TS_REG, 1u << (p - 32)); }
static inline void PIN_CLR(int p) { if (p < 32) REG_WRITE(GPIO_OUT_W1TC_REG, 1u << p); else REG_WRITE(GPIO_OUT1_W1TC_REG, 1u << (p - 32)); }
static inline uint32_t PIN_IN(int p) { return p < 32 ? (REG_READ(GPIO_IN_REG) >> p) & 1u : (REG_READ(GPIO_IN1_REG) >> (p - 32)) & 1u; }

static bool s_slow;
static bool s_ready;

void hal_sdspi_begin(void)
{
    if (s_ready) return;
    hal_gpio_config_output(PIN_CS, 1);
    hal_gpio_config_output(PIN_SCLK, 0);                 /* SPI mode 0: idle low */
    hal_gpio_config_output(PIN_MOSI, 1);
    hal_gpio_config_input_pullup(PIN_MISO);
    s_ready = true;
}

void hal_sdspi_end(void) { }
void hal_sdspi_cs(bool selected) { if (selected) PIN_CLR(PIN_CS); else PIN_SET(PIN_CS); }
void hal_sdspi_set_slow(bool slow) { s_slow = slow; }

uint8_t hal_sdspi_xfer(uint8_t out)
{
    uint8_t in = 0;
    for (int i = 7; i >= 0; i--) {
        if (out & (1u << i)) PIN_SET(PIN_MOSI); else PIN_CLR(PIN_MOSI);
        if (s_slow) esp_rom_delay_us(SLOW_HALF_PERIOD_US);
        PIN_SET(PIN_SCLK);
        if (s_slow) esp_rom_delay_us(SLOW_HALF_PERIOD_US);
        in = (uint8_t)((in << 1) | (uint8_t)PIN_IN(PIN_MISO));
        PIN_CLR(PIN_SCLK);
    }
    return in;
}
