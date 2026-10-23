#pragma once
#include <m5os/errno.h>
#include <m5os/types.h>
#define IO_API_VERSION 1u
#define IO_ERR_NACK (-19)
#define IO_ERR_CANCELLED (-20)
#define IO_PIN_COUNT 55
#define IO_OWNER_NAME_MAX 20
/* Trusted in-process resource ownership, not a security boundary. IDs never
 * reused. */
typedef uint32_t io_owner_t;
typedef enum {
  IO_USE_NONE,
  IO_USE_DIGITAL,
  IO_USE_ADC,
  IO_USE_PWM,
  IO_USE_I2C,
  IO_USE_SPI,
  IO_USE_UART
} io_use_t;
typedef enum { IO_PULL_NONE, IO_PULL_UP, IO_PULL_DOWN } io_pull_t;
typedef struct {
  int pin;
  bool exposed, reserved;
  io_owner_t owner;
  io_use_t use;
  const char *label;
  char owner_name[IO_OWNER_NAME_MAX];
} io_pin_info_t;
int io_owner_create(const char *name, io_owner_t *out);
bool io_owner_valid(io_owner_t owner);
int io_owner_close(io_owner_t owner);
/* Task-context deferred cleanup; never call from task-kill interrupt sections.
 */
void io_reap_dead_owners(void);
/* Atomic all-or-nothing, rejects duplicates, system pins, and already claimed
 * pins even by the same owner (separate peripheral objects cannot overlap). */
int io_pins_claim(io_owner_t owner, const int *pins, size_t count,
                  io_use_t use);
int io_pins_release(io_owner_t owner, const int *pins, size_t count);
int io_pin_info(int pin, io_pin_info_t *out);
/* Board keyboard startup releases Ext.Port1 only if no I2C keyboard was found.
 */
void io_tab5_keyboard_detected(bool present);
int io_gpio_config(io_owner_t owner, int pin, bool output, int initial,
                   io_pull_t pull);
int io_gpio_read(io_owner_t owner, int pin, int *value);
int io_gpio_write(io_owner_t owner, int pin, int value);
int io_adc_read(io_owner_t owner, int pin, unsigned attenuation_db, int *raw);
int io_pwm_set(io_owner_t owner, int pin, uint32_t frequency_hz,
               unsigned duty_permille, uint32_t *actual_hz);
int io_pwm_stop(io_owner_t owner, int pin);
const char *io_error_name(int error);
