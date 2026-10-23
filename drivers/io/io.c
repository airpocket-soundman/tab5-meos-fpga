#include <m5os/hal_gpio.h>
#include <m5os/hal_io_analog.h>
#include <m5os/hal_io_gpio.h>
#include <m5os/io.h>
#include <m5os/io_i2c.h>
#include <m5os/io_spi.h>
#include <m5os/io_uart.h>
#include <m5os/mutex.h>
#include <string.h>
#define OWNERS 8
static mutex_t lock = MUTEX_INIT;
static struct {
  io_owner_t id;
  bool closing;
  uint32_t creator_pid, closer_pid;
  char name[IO_OWNER_NAME_MAX];
} owners[OWNERS];
static struct {
  io_owner_t owner;
  io_use_t use;
  bool output;
} pins[IO_PIN_COUNT];
static uint32_t next_owner = 1;
static bool keyboard_reserved = true;
static int owner_index(io_owner_t id) {
  for (int i = 0; i < OWNERS; i++)
    if (id && owners[i].id == id)
      return i;
  return -1;
}
static bool external(int p) {
#if M5OS_BOARD_TAB5
  switch (p) {
  case 0:
  case 1:
  case 2:
  case 3:
  case 4:
  case 5:
  case 6:
  case 7:
  case 16:
  case 17:
  case 18:
  case 19:
  case 35:
  case 45:
  case 47:
  case 48:
  case 50:
  case 51:
  case 52:
  case 53:
  case 54:
    return true;
  }
#endif
  (void)p;
  return false;
}
static const char *label(int p) {
  if (p == 0 || p == 1 || p == 50)
    return keyboard_reserved ? "Ext.Port1 keyboard" : "Ext.Port1";
  if (p == 31 || p == 32)
    return "system I2C";
  if (p >= 8 && p <= 15)
    return "C6 SDIO/control";
  if (p >= 39 && p <= 44)
    return "microSD";
  if (p == 37 || p == 38)
    return "console/debug UART";
  if (p == 20 || p == 21 || p == 34)
    return "RS485";
  if (p == 22)
    return "LCD backlight";
  if (p == 23)
    return "touch interrupt";
  if (p == 36)
    return "camera clock";
  if (p >= 26 && p <= 30)
    return "audio";
  if (p == 53 || p == 54)
    return "PORT.A";
  return external(p) ? "M5-Bus/external" : "board/reserved";
}
static bool reserved(int p) {
  return !external(p) || (keyboard_reserved && (p == 0 || p == 1 || p == 50));
}
void io_tab5_keyboard_detected(bool present) {
  mutex_lock(&lock);
  keyboard_reserved = present;
  mutex_unlock(&lock);
}
static bool task_alive(uint32_t pid) {
  if (!pid)
    return true;
  task_info_t ts[TASK_MAX];
  int n = task_snapshot(ts, TASK_MAX);
  for (int i = 0; i < n; i++)
    if (ts[i].id == pid && ts[i].state != TASK_TERMINATED)
      return true;
  return false;
}
void io_reap_dead_owners(void) {
  io_owner_t dead[OWNERS];
  unsigned n = 0;
  mutex_lock(&lock);
  for (unsigned i = 0; i < OWNERS; i++)
    if (owners[i].id &&
        (!task_alive(owners[i].creator_pid) ||
         (owners[i].closing && !task_alive(owners[i].closer_pid))))
      dead[n++] = owners[i].id;
  mutex_unlock(&lock);
  for (unsigned i = 0; i < n; i++)
    io_owner_close(dead[i]);
}
int io_owner_create(const char *name, io_owner_t *out) {
  if (out)
    *out = 0;
  io_reap_dead_owners();
  if (!name || !name[0] || !out)
    return ERR_INVAL;
  mutex_lock(&lock);
  int rc = ERR_BUSY;
  for (unsigned i = 0; i < OWNERS; i++)
    if (!owners[i].id) {
      if (!next_owner) {
        rc = ERR_NOSPC;
        break;
      }
      owners[i].id = next_owner++;
      owners[i].closing = false;
      task_t *self = task_current();
      owners[i].creator_pid = self ? self->id : 0;
      size_t j = 0;
      while (j < IO_OWNER_NAME_MAX - 1 && name[j]) {
        owners[i].name[j] = name[j];
        j++;
      }
      owners[i].name[j] = 0;
      *out = owners[i].id;
      rc = 0;
      break;
    }
  mutex_unlock(&lock);
  return rc;
}
bool io_owner_valid(io_owner_t owner) {
  mutex_lock(&lock);
  int i = owner_index(owner);
  bool ok = i >= 0 && !owners[i].closing;
  mutex_unlock(&lock);
  return ok;
}
int io_pin_info(int p, io_pin_info_t *out) {
  if (p < 0 || p >= IO_PIN_COUNT || !out)
    return ERR_INVAL;
  mutex_lock(&lock);
  memset(out, 0, sizeof *out);
  out->pin = p;
  out->exposed = external(p);
  out->reserved = reserved(p);
  out->label = label(p);
  out->owner = pins[p].owner;
  out->use = pins[p].use;
  int i = owner_index(out->owner);
  if (i >= 0)
    memcpy(out->owner_name, owners[i].name, IO_OWNER_NAME_MAX);
  mutex_unlock(&lock);
  return 0;
}
static int validate(io_owner_t owner, const int *ps, size_t count,
                    bool release) {
  if (!ps || !count || count > IO_PIN_COUNT)
    return ERR_INVAL;
  int oi = owner_index(owner);
  if (oi < 0 || (!release && owners[oi].closing))
    return ERR_BADF;
  for (size_t i = 0; i < count; i++) {
    int p = ps[i];
    if (p < 0 || p >= IO_PIN_COUNT)
      return ERR_INVAL;
    for (size_t j = 0; j < i; j++)
      if (ps[j] == p)
        return ERR_INVAL;
    if (release) {
      if (pins[p].owner != owner)
        return ERR_PERM;
    } else {
      if (reserved(p))
        return ERR_PERM;
      if (pins[p].owner)
        return ERR_BUSY;
    }
  }
  return 0;
}
int io_pins_claim(io_owner_t owner, const int *ps, size_t count, io_use_t use) {
  if (use <= IO_USE_NONE || use > IO_USE_UART)
    return ERR_INVAL;
  mutex_lock(&lock);
  int rc = validate(owner, ps, count, false);
  if (!rc)
    for (size_t i = 0; i < count; i++) {
      pins[ps[i]].owner = owner;
      pins[ps[i]].use = use;
      pins[ps[i]].output = false;
    }
  mutex_unlock(&lock);
  return rc;
}
int io_pins_release(io_owner_t owner, const int *ps, size_t count) {
  mutex_lock(&lock);
  int rc = validate(owner, ps, count, true);
  if (!rc)
    for (size_t i = 0; i < count; i++) {
#if M5OS_BOARD_TAB5
      if (pins[ps[i]].use == IO_USE_PWM)
        hal_io_pwm_stop(ps[i]);
#endif
      hal_io_gpio_release(ps[i]);
      memset(&pins[ps[i]], 0, sizeof pins[ps[i]]);
    }
  mutex_unlock(&lock);
  return rc;
}
static int owned(io_owner_t o, int p, io_use_t use) {
  int i = owner_index(o);
  if (i < 0 || owners[i].closing)
    return ERR_BADF;
  if (p < 0 || p >= IO_PIN_COUNT)
    return ERR_INVAL;
  return pins[p].owner == o && pins[p].use == use ? 0 : ERR_PERM;
}
int io_gpio_config(io_owner_t o, int p, bool output, int initial,
                   io_pull_t pull) {
  if ((initial != 0 && initial != 1) || pull > IO_PULL_DOWN ||
      pull < IO_PULL_NONE || (output && pull != IO_PULL_NONE))
    return ERR_INVAL;
  mutex_lock(&lock);
  int rc = owned(o, p, IO_USE_DIGITAL);
  if (!rc) {
    if (output)
      hal_io_gpio_output(p, initial);
    else
      hal_io_gpio_input(p, pull);
    pins[p].output = output;
  }
  mutex_unlock(&lock);
  return rc;
}
int io_gpio_read(io_owner_t o, int p, int *value) {
  if (!value)
    return ERR_INVAL;
  mutex_lock(&lock);
  int rc = owned(o, p, IO_USE_DIGITAL);
  if (!rc)
    *value = hal_gpio_read(p);
  mutex_unlock(&lock);
  return rc;
}
int io_gpio_write(io_owner_t o, int p, int value) {
  if (value != 0 && value != 1)
    return ERR_INVAL;
  mutex_lock(&lock);
  int rc = owned(o, p, IO_USE_DIGITAL);
  if (!rc && !pins[p].output)
    rc = ERR_PERM;
  if (!rc)
    hal_gpio_write(p, value);
  mutex_unlock(&lock);
  return rc;
}
int io_adc_read(io_owner_t o, int p, unsigned atten, int *raw) {
#if M5OS_BOARD_TAB5
  if (!raw || atten > 3)
    return ERR_INVAL;
  mutex_lock(&lock);
  int rc = owned(o, p, IO_USE_ADC);
  uint16_t value = 0;
  if (!rc)
    rc = hal_io_adc_read_raw(p, atten, &value);
  if (!rc)
    *raw = value;
  mutex_unlock(&lock);
  return rc;
#else
  (void)o;
  (void)p;
  (void)atten;
  (void)raw;
  return ERR_NOSYS;
#endif
}
int io_pwm_set(io_owner_t o, int p, uint32_t hz, unsigned duty,
               uint32_t *actual) {
#if M5OS_BOARD_TAB5
  mutex_lock(&lock);
  int rc = owned(o, p, IO_USE_PWM);
  if (!rc)
    rc = hal_io_pwm_set(p, hz, duty, actual);
  mutex_unlock(&lock);
  return rc;
#else
  (void)o;
  (void)p;
  (void)hz;
  (void)duty;
  (void)actual;
  return ERR_NOSYS;
#endif
}
int io_pwm_stop(io_owner_t o, int p) {
#if M5OS_BOARD_TAB5
  mutex_lock(&lock);
  int rc = owned(o, p, IO_USE_PWM);
  if (!rc)
    rc = hal_io_pwm_stop(p);
  mutex_unlock(&lock);
  return rc;
#else
  (void)o;
  (void)p;
  return ERR_NOSYS;
#endif
}
int io_owner_close(io_owner_t o) {
  mutex_lock(&lock);
  int i = owner_index(o);
  if (i < 0) {
    mutex_unlock(&lock);
    return ERR_BADF;
  }
  if (owners[i].closing && task_alive(owners[i].closer_pid)) {
    mutex_unlock(&lock);
    return ERR_BUSY;
  }
  owners[i].closing = true;
  task_t *self = task_current();
  owners[i].closer_pid = self ? self->id : 0;
  mutex_unlock(&lock);
  /* Never hold pin-registry lock while waiting for a protocol transaction. */
  io_i2c_release_owner(o);
  io_spi_release_owner(o);
  io_uart_release_owner(o);
  mutex_lock(&lock);
  for (int p = 0; p < IO_PIN_COUNT; p++)
    if (pins[p].owner == o) {
#if M5OS_BOARD_TAB5
      if (pins[p].use == IO_USE_PWM)
        hal_io_pwm_stop(p);
#endif
      hal_io_gpio_release(p);
      memset(&pins[p], 0, sizeof pins[p]);
    }
  memset(&owners[i], 0, sizeof owners[i]);
  mutex_unlock(&lock);
  return 0;
}
const char *io_error_name(int rc) {
  switch (rc) {
  case 0:
    return "ok";
  case ERR_BADF:
    return "closed/invalid owner or handle";
  case ERR_INVAL:
    return "invalid argument";
  case ERR_PERM:
    return "reserved pin, wrong owner or mode";
  case ERR_BUSY:
    return "resource busy";
  case ERR_TIMEOUT:
    return "timeout";
  case IO_ERR_NACK:
    return "I2C NACK";
  case IO_ERR_CANCELLED:
    return "cancelled";
  case ERR_NOSYS:
    return "unsupported";
  default:
    return "I/O error";
  }
}
