#include "esp_rom_gpio.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_sig_map.h"
#include "soc/gpio_struct.h"
#include <m5os/hal_gpio.h>
#include <m5os/hal_io_gpio.h>
void hal_io_gpio_input(int pin, int pull) {
  esp_rom_gpio_pad_select_gpio(pin);
  gpio_ll_output_disable(&GPIO, pin);
  gpio_ll_od_disable(&GPIO, pin);
  gpio_ll_input_enable(&GPIO, pin);
  gpio_ll_pullup_dis(&GPIO, pin);
  gpio_ll_pulldown_dis(&GPIO, pin);
  if (pull == 1)
    gpio_ll_pullup_en(&GPIO, pin);
  if (pull == 2)
    gpio_ll_pulldown_en(&GPIO, pin);
}
void hal_io_gpio_output(int pin, int initial) {
  gpio_ll_output_disable(&GPIO, pin);
  esp_rom_gpio_pad_select_gpio(pin);
  esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, false, false);
  gpio_ll_od_disable(&GPIO, pin);
  gpio_ll_pullup_dis(&GPIO, pin);
  gpio_ll_pulldown_dis(&GPIO, pin);
  gpio_ll_set_level(&GPIO, pin, initial);
  gpio_ll_input_enable(&GPIO, pin);
  gpio_ll_output_enable(&GPIO, pin);
}
void hal_io_gpio_release(int pin) { hal_io_gpio_input(pin, 0); }
