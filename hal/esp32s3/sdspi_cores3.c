/*
 * SD card on the CoreS3 shared SPI bus (M5GFX: SD chip select = GPIO4,
 * SCLK=36, MOSI=37, MISO=35 - the same GPIO35 the LCD uses as D/C).
 *
 * While the SD card is being accessed GPIO35 is an INPUT (MISO); the LCD driver
 * expects it to be an output (D/C), so hal_sdspi_end() restores that.  LCD CS
 * (GPIO3) must stay high meanwhile; hal_lcd_end() leaves it high.
 */
#include <m5os/hal_sdspi.h>
#include <m5os/hal_gpio.h>
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"
#include "esp_rom_sys.h"

#define PIN_SD_CS     4
#define PIN_SCLK      36
#define PIN_MOSI      37
#define PIN_MISO      35
#define BANK1_BIT(p)  (1u << ((p) - 32))
#define SCLK_BIT      BANK1_BIT(PIN_SCLK)
#define MOSI_BIT      BANK1_BIT(PIN_MOSI)
#define MISO_BIT      BANK1_BIT(PIN_MISO)
#define SLOW_HALF_PERIOD_US  2          /* ~250 kHz: inside the 100-400 kHz init window */

static bool s_slow;
static bool s_cs_ready;

void hal_sdspi_begin(void)
{
    if (!s_cs_ready) {
        hal_gpio_config_output(PIN_SD_CS, 1);
        s_cs_ready = true;
    }
    gpio_ll_output_disable(&GPIO, PIN_MISO);      /* GPIO35: D/C output -> MISO input */
    gpio_ll_input_enable(&GPIO, PIN_MISO);
}

void hal_sdspi_end(void)
{
    gpio_ll_set_level(&GPIO, PIN_MISO, 1);        /* LCD D/C idles high */
    gpio_ll_output_enable(&GPIO, PIN_MISO);
}

void hal_sdspi_cs(bool selected) { gpio_ll_set_level(&GPIO, PIN_SD_CS, selected ? 0 : 1); }
void hal_sdspi_set_slow(bool slow) { s_slow = slow; }

uint8_t hal_sdspi_xfer(uint8_t out)
{
    uint8_t in = 0;
    for (int i = 7; i >= 0; i--) {
        if (out & (1u << i)) REG_WRITE(GPIO_OUT1_W1TS_REG, MOSI_BIT);
        else                 REG_WRITE(GPIO_OUT1_W1TC_REG, MOSI_BIT);
        if (s_slow) esp_rom_delay_us(SLOW_HALF_PERIOD_US);
        REG_WRITE(GPIO_OUT1_W1TS_REG, SCLK_BIT);              /* card samples MOSI / drives MISO */
        if (s_slow) esp_rom_delay_us(SLOW_HALF_PERIOD_US);
        in = (uint8_t)((in << 1) | ((REG_READ(GPIO_IN1_REG) & MISO_BIT) ? 1u : 0u));
        REG_WRITE(GPIO_OUT1_W1TC_REG, SCLK_BIT);
    }
    return in;
}
