/* MeOS-owned external GPIO bindings; no ESP-IDF driver or FreeRTOS API calls.
 * Objects do not alias pins: another object must first explicitly deinit().
 * The port must invoke mp_machine_io_deinit on every VM exit/reset path. */
#include "machine_meos.h"
#include "py/mperrno.h"
#include "py/runtime.h"
#include <m5os/io.h>
static io_owner_t machine_owner;
void mp_machine_io_deinit(void) {
  io_owner_t owner = machine_owner;
  machine_owner = 0;
  if (owner)
    io_owner_close(owner);
}
void mp_machine_io_init(void) { mp_machine_io_deinit(); }
void mp_machine_error(int rc) {
  if (rc >= 0)
    return;
  int error = MP_EIO;
  switch (rc) {
  case ERR_INVAL:
    error = MP_EINVAL;
    break;
  case ERR_BADF:
    error = MP_EBADF;
    break;
  case ERR_PERM:
    error = MP_EPERM;
    break;
  case ERR_BUSY:
    error = MP_EBUSY;
    break;
  case ERR_NOMEM:
    error = MP_ENOMEM;
    break;
  case ERR_NODEV:
    error = MP_ENODEV;
    break;
  case ERR_NOSYS:
    error = MP_EOPNOTSUPP;
    break;
  case ERR_TIMEOUT:
    error = MP_ETIMEDOUT;
    break;
  case IO_ERR_CANCELLED:
    error = MP_EINTR;
    break;
  }
  mp_raise_OSError(error);
}
io_owner_t mp_machine_owner(void) {
  if (machine_owner && !io_owner_valid(machine_owner))
    machine_owner = 0;
  if (!machine_owner)
    mp_machine_error(io_owner_create("micropython", &machine_owner));
  return machine_owner;
}
void mp_machine_owner_check(io_owner_t owner) {
  if (!owner || owner != machine_owner || !io_owner_valid(owner))
    mp_raise_OSError(MP_EBADF);
}
typedef struct {
  mp_obj_base_t base;
  io_owner_t owner;
  int pin;
  bool active, output;
  io_pull_t pull;
  int value;
} meos_pin_t;
static const mp_obj_type_t meos_pin_type;
static meos_pin_t *pin_object(mp_obj_t obj) {
  if (!mp_obj_is_type(obj, &meos_pin_type))
    mp_raise_TypeError(MP_ERROR_TEXT("expected Pin"));
  return MP_OBJ_TO_PTR(obj);
}
static void pin_check(meos_pin_t *self) {
  if (!self->active || self->owner != machine_owner ||
      !io_owner_valid(self->owner))
    mp_raise_OSError(MP_EBADF);
}
static void pin_config(meos_pin_t *self, size_t count, const mp_obj_t *pos,
                       mp_map_t *kw, bool constructor) {
  enum { MODE, PULL, VALUE };
  static const mp_arg_t allowed[] = {
      {MP_QSTR_mode, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_pull, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_value, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
  };
  mp_arg_val_t args[MP_ARRAY_SIZE(allowed)];
  mp_arg_parse_all(count, pos, kw, MP_ARRAY_SIZE(allowed), allowed, args);
  bool output = constructor ? false : self->output;
  io_pull_t pull = constructor ? IO_PULL_NONE : self->pull;
  int value = constructor ? 0 : self->value;
  if (args[MODE].u_obj != MP_OBJ_NULL) {
    mp_int_t mode = mp_obj_get_int(args[MODE].u_obj);
    if (mode != 0 && mode != 1)
      mp_raise_ValueError(
          MP_ERROR_TEXT("only Pin.IN and Pin.OUT are supported"));
    output = mode == 1;
  }
  if (args[PULL].u_obj != MP_OBJ_NULL) {
    if (args[PULL].u_obj == mp_const_none)
      pull = IO_PULL_NONE;
    else {
      mp_int_t p = mp_obj_get_int(args[PULL].u_obj);
      if (p < IO_PULL_NONE || p > IO_PULL_DOWN)
        mp_raise_ValueError(MP_ERROR_TEXT("invalid pull"));
      pull = (io_pull_t)p;
    }
  }
  if (args[VALUE].u_obj != MP_OBJ_NULL)
    value = mp_obj_is_true(args[VALUE].u_obj);
  if (output && pull != IO_PULL_NONE)
    mp_raise_ValueError(MP_ERROR_TEXT("output pull is unsupported"));
  if (!output && args[VALUE].u_obj != MP_OBJ_NULL)
    mp_raise_ValueError(MP_ERROR_TEXT("value requires Pin.OUT"));
  if (constructor) {
    self->owner = mp_machine_owner();
    mp_machine_error(io_pins_claim(self->owner, &self->pin, 1, IO_USE_DIGITAL));
    int rc = io_gpio_config(self->owner, self->pin, output, value, pull);
    if (rc < 0) {
      io_pins_release(self->owner, &self->pin, 1);
      mp_machine_error(rc);
    }
    self->active = true;
  } else {
    pin_check(self);
    mp_machine_error(
        io_gpio_config(self->owner, self->pin, output, value, pull));
  }
  self->output = output;
  self->pull = pull;
  self->value = value;
}
static mp_obj_t pin_new(const mp_obj_type_t *type, size_t n, size_t nk,
                        const mp_obj_t *args) {
  mp_arg_check_num(n, nk, 1, 3, true);
  mp_int_t pin = mp_obj_get_int(args[0]);
  if (pin < 0 || pin >= IO_PIN_COUNT)
    mp_raise_ValueError(MP_ERROR_TEXT("invalid GPIO"));
  meos_pin_t *self = mp_obj_malloc(meos_pin_t, type);
  self->pin = (int)pin;
  self->active = false;
  self->owner = 0;
  mp_map_t kw;
  mp_map_init_fixed_table(&kw, nk, args + n);
  pin_config(self, n - 1, args + 1, &kw, true);
  return MP_OBJ_FROM_PTR(self);
}
static mp_obj_t pin_init(size_t n, const mp_obj_t *args, mp_map_t *kw) {
  pin_config(pin_object(args[0]), n - 1, args + 1, kw, false);
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(pin_init_obj, 1, pin_init);
static mp_obj_t pin_value(size_t n, const mp_obj_t *args) {
  meos_pin_t *self = pin_object(args[0]);
  pin_check(self);
  if (n == 1) {
    int value = 0;
    mp_machine_error(io_gpio_read(self->owner, self->pin, &value));
    return mp_obj_new_int(value);
  }
  int value = mp_obj_is_true(args[1]);
  pin_check(self);
  mp_machine_error(io_gpio_write(self->owner, self->pin, value));
  self->value = value;
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pin_value_obj, 1, 2, pin_value);
static mp_obj_t pin_on(mp_obj_t obj) {
  meos_pin_t *self = pin_object(obj);
  pin_check(self);
  mp_machine_error(io_gpio_write(self->owner, self->pin, 1));
  self->value = 1;
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(pin_on_obj, pin_on);
static mp_obj_t pin_off(mp_obj_t obj) {
  meos_pin_t *self = pin_object(obj);
  pin_check(self);
  mp_machine_error(io_gpio_write(self->owner, self->pin, 0));
  self->value = 0;
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(pin_off_obj, pin_off);
static mp_obj_t pin_deinit(mp_obj_t obj) {
  meos_pin_t *self = pin_object(obj);
  if (self->active) {
    pin_check(self);
    mp_machine_error(io_pins_release(self->owner, &self->pin, 1));
    self->active = false;
  }
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(pin_deinit_obj, pin_deinit);
static const mp_rom_map_elem_t pin_locals[] = {
    {MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&pin_init_obj)},
    {MP_ROM_QSTR(MP_QSTR_value), MP_ROM_PTR(&pin_value_obj)},
    {MP_ROM_QSTR(MP_QSTR_on), MP_ROM_PTR(&pin_on_obj)},
    {MP_ROM_QSTR(MP_QSTR_off), MP_ROM_PTR(&pin_off_obj)},
    {MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&pin_deinit_obj)},
    {MP_ROM_QSTR(MP_QSTR_IN), MP_ROM_INT(0)},
    {MP_ROM_QSTR(MP_QSTR_OUT), MP_ROM_INT(1)},
    {MP_ROM_QSTR(MP_QSTR_PULL_UP), MP_ROM_INT(IO_PULL_UP)},
    {MP_ROM_QSTR(MP_QSTR_PULL_DOWN), MP_ROM_INT(IO_PULL_DOWN)},
};
static MP_DEFINE_CONST_DICT(pin_locals_dict, pin_locals);
static MP_DEFINE_CONST_OBJ_TYPE(meos_pin_type, MP_QSTR_Pin, MP_TYPE_FLAG_NONE,
                                make_new, pin_new, locals_dict,
                                &pin_locals_dict);
/* ADC numbers are GPIO IDs. read_u16 scales the uncalibrated 12-bit code;
 * it does not report voltage or apply eFuse/reference calibration. */
typedef struct {
  mp_obj_base_t base;
  io_owner_t owner;
  int pin;
  unsigned attenuation;
  bool active;
} meos_adc_t;
static const mp_obj_type_t meos_adc_type;
static meos_adc_t *adc_object(mp_obj_t obj) {
  if (!mp_obj_is_type(obj, &meos_adc_type))
    mp_raise_TypeError(MP_ERROR_TEXT("expected ADC"));
  return MP_OBJ_TO_PTR(obj);
}
static void adc_check(meos_adc_t *self) {
  if (!self->active || self->owner != machine_owner ||
      !io_owner_valid(self->owner))
    mp_raise_OSError(MP_EBADF);
}
static mp_obj_t adc_new(const mp_obj_type_t *type, size_t n, size_t nk,
                        const mp_obj_t *args) {
  enum { ID, ATTEN };
  static const mp_arg_t allowed[] = {
      {MP_QSTR_id, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0}},
      {MP_QSTR_atten, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0}},
  };
  mp_arg_val_t values[MP_ARRAY_SIZE(allowed)];
  mp_arg_parse_all_kw_array(n, nk, args, MP_ARRAY_SIZE(allowed), allowed,
                            values);
  mp_int_t pin = values[ID].u_int, atten = values[ATTEN].u_int;
  if (pin < 0 || pin >= IO_PIN_COUNT || atten < 0 || atten > 3)
    mp_raise_ValueError(MP_ERROR_TEXT("invalid ADC pin/attenuation"));
  meos_adc_t *self = mp_obj_malloc(meos_adc_t, type);
  self->pin = (int)pin;
  self->attenuation = (unsigned)atten;
  self->active = false;
  self->owner = mp_machine_owner();
  mp_machine_error(io_pins_claim(self->owner, &self->pin, 1, IO_USE_ADC));
  /* Validate supported ADC GPIO through the shared service, with a first
   * read. Never bypass its owner, board-pin or channel checks. */
  int raw = 0,
      rc = io_adc_read(self->owner, self->pin, self->attenuation, &raw);
  if (rc < 0) {
    io_pins_release(self->owner, &self->pin, 1);
    mp_machine_error(rc);
  }
  self->active = true;
  return MP_OBJ_FROM_PTR(self);
}
static mp_obj_t adc_read(mp_obj_t obj) {
  meos_adc_t *self = adc_object(obj);
  adc_check(self);
  int raw = 0;
  mp_machine_error(
      io_adc_read(self->owner, self->pin, self->attenuation, &raw));
  return mp_obj_new_int(raw);
}
static MP_DEFINE_CONST_FUN_OBJ_1(adc_read_obj, adc_read);
static mp_obj_t adc_read_u16(mp_obj_t obj) {
  meos_adc_t *self = adc_object(obj);
  adc_check(self);
  int raw = 0;
  mp_machine_error(
      io_adc_read(self->owner, self->pin, self->attenuation, &raw));
  if (raw < 0 || raw > 4095)
    mp_raise_OSError(MP_EIO);
  return mp_obj_new_int((raw << 4) | (raw >> 8));
}
static MP_DEFINE_CONST_FUN_OBJ_1(adc_read_u16_obj, adc_read_u16);
static mp_obj_t adc_atten(mp_obj_t obj, mp_obj_t value) {
  meos_adc_t *self = adc_object(obj);
  adc_check(self);
  mp_int_t attenuation = mp_obj_get_int(value);
  if (attenuation < 0 || attenuation > 3)
    mp_raise_ValueError(MP_ERROR_TEXT("attenuation must be 0..3"));
  adc_check(self);
  self->attenuation = (unsigned)attenuation;
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(adc_atten_obj, adc_atten);
static mp_obj_t adc_deinit(mp_obj_t obj) {
  meos_adc_t *self = adc_object(obj);
  if (self->active) {
    adc_check(self);
    mp_machine_error(io_pins_release(self->owner, &self->pin, 1));
    self->active = false;
  }
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(adc_deinit_obj, adc_deinit);
static const mp_rom_map_elem_t adc_locals[] = {
    {MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&adc_read_obj)},
    {MP_ROM_QSTR(MP_QSTR_read_u16), MP_ROM_PTR(&adc_read_u16_obj)},
    {MP_ROM_QSTR(MP_QSTR_atten), MP_ROM_PTR(&adc_atten_obj)},
    {MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&adc_deinit_obj)},
    {MP_ROM_QSTR(MP_QSTR_ATTN_0DB), MP_ROM_INT(0)},
    {MP_ROM_QSTR(MP_QSTR_ATTN_2_5DB), MP_ROM_INT(1)},
    {MP_ROM_QSTR(MP_QSTR_ATTN_6DB), MP_ROM_INT(2)},
    {MP_ROM_QSTR(MP_QSTR_ATTN_11DB), MP_ROM_INT(3)},
};
static MP_DEFINE_CONST_DICT(adc_locals_dict, adc_locals);
static MP_DEFINE_CONST_OBJ_TYPE(meos_adc_type, MP_QSTR_ADC, MP_TYPE_FLAG_NONE,
                                make_new, adc_new, locals_dict,
                                &adc_locals_dict);
static const mp_rom_map_elem_t machine_globals[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_machine)},
    {MP_ROM_QSTR(MP_QSTR_Pin), MP_ROM_PTR(&meos_pin_type)},
    {MP_ROM_QSTR(MP_QSTR_ADC), MP_ROM_PTR(&meos_adc_type)},
    {MP_ROM_QSTR(MP_QSTR_PWM), MP_ROM_PTR(&mp_machine_pwm_type)},
    {MP_ROM_QSTR(MP_QSTR_UART), MP_ROM_PTR(&mp_machine_uart_type)},
    {MP_ROM_QSTR(MP_QSTR_SoftI2C), MP_ROM_PTR(&mp_machine_soft_i2c_type)},
    {MP_ROM_QSTR(MP_QSTR_SoftSPI), MP_ROM_PTR(&mp_machine_soft_spi_type)},
};
static MP_DEFINE_CONST_DICT(machine_globals_dict, machine_globals);
const mp_obj_module_t mp_module_machine = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&machine_globals_dict};
MP_REGISTER_MODULE(MP_QSTR_machine, mp_module_machine);
