/* PWM over the common MeOS IO owner. Numeric GPIO IDs only; no Pin aliases.
 * Duty requests are rounded to the shared service's 0..1000 scale. The getter
 * retains the requested u16 value; frequency reports the returned actual Hz. */
#include "machine_meos.h"
#include "py/mperrno.h"
#include "py/runtime.h"
#include <m5os/io.h>

typedef struct {
  mp_obj_base_t base;
  io_owner_t owner;
  int pin;
  uint32_t requested_hz, actual_hz;
  unsigned duty;
  bool active;
} meos_pwm_t;
static meos_pwm_t *pwm_object(mp_obj_t obj) {
  if (!mp_obj_is_type(obj, &mp_machine_pwm_type))
    mp_raise_TypeError(MP_ERROR_TEXT("expected PWM"));
  return MP_OBJ_TO_PTR(obj);
}
static void pwm_check(meos_pwm_t *self) {
  if (!self->active)
    mp_raise_OSError(MP_EBADF);
  mp_machine_owner_check(self->owner);
}
static uint32_t pwm_frequency(mp_int_t hz) {
  if (hz < 1 || hz > 40000000)
    mp_raise_ValueError(MP_ERROR_TEXT("frequency must be 1..40000000"));
  return (uint32_t)hz;
}
static unsigned pwm_duty(mp_int_t duty) {
  if (duty < 0 || duty > 65535)
    mp_raise_ValueError(MP_ERROR_TEXT("duty_u16 must be 0..65535"));
  return (unsigned)duty;
}
static unsigned pwm_permille(unsigned duty) {
  return (duty * 1000u + 32767u) / 65535u;
}
static mp_obj_t pwm_new(const mp_obj_type_t *type, size_t n, size_t nk,
                        const mp_obj_t *args) {
  enum { PIN, FREQ, DUTY };
  static const mp_arg_t allowed[] = {
      {MP_QSTR_pin, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_freq, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1000}},
      {MP_QSTR_duty_u16, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0}},
  };
  mp_arg_val_t values[MP_ARRAY_SIZE(allowed)];
  mp_arg_parse_all_kw_array(n, nk, args, MP_ARRAY_SIZE(allowed), allowed,
                            values);
  if (!mp_obj_is_int(values[PIN].u_obj))
    mp_raise_TypeError(MP_ERROR_TEXT("PWM pin must be an integer GPIO"));
  mp_int_t pin = mp_obj_get_int(values[PIN].u_obj);
  if (pin < 0 || pin >= IO_PIN_COUNT)
    mp_raise_ValueError(MP_ERROR_TEXT("invalid GPIO"));
  uint32_t hz = pwm_frequency(values[FREQ].u_int);
  unsigned duty = pwm_duty(values[DUTY].u_int);
  meos_pwm_t *self = mp_obj_malloc(meos_pwm_t, type);
  self->active = false;
  self->pin = (int)pin;
  self->owner = mp_machine_owner();
  mp_machine_error(io_pins_claim(self->owner, &self->pin, 1, IO_USE_PWM));
  uint32_t actual = 0;
  int rc = io_pwm_set(self->owner, self->pin, hz, pwm_permille(duty), &actual);
  if (rc < 0) {
    io_pins_release(self->owner, &self->pin, 1);
    mp_machine_error(rc);
  }
  /* No VM allocation/callback occurs between the successful claim and record.
   */
  self->requested_hz = hz;
  self->actual_hz = actual;
  self->duty = duty;
  self->active = true;
  return MP_OBJ_FROM_PTR(self);
}
static mp_obj_t pwm_freq(size_t n, const mp_obj_t *args) {
  meos_pwm_t *self = pwm_object(args[0]);
  if (n == 1) {
    pwm_check(self);
    return mp_obj_new_int_from_uint(self->actual_hz);
  }
  uint32_t hz = pwm_frequency(mp_obj_get_int(args[1]));
  pwm_check(self); /* __int__ conversion may have reentered Python. */
  uint32_t actual = 0;
  mp_machine_error(io_pwm_set(self->owner, self->pin, hz,
                              pwm_permille(self->duty), &actual));
  self->requested_hz = hz;
  self->actual_hz = actual;
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pwm_freq_obj, 1, 2, pwm_freq);
static mp_obj_t pwm_duty_u16(size_t n, const mp_obj_t *args) {
  meos_pwm_t *self = pwm_object(args[0]);
  if (n == 1) {
    pwm_check(self);
    return mp_obj_new_int_from_uint(self->duty);
  }
  unsigned duty = pwm_duty(mp_obj_get_int(args[1]));
  pwm_check(self);
  uint32_t actual = 0;
  mp_machine_error(io_pwm_set(self->owner, self->pin, self->requested_hz,
                              pwm_permille(duty), &actual));
  self->actual_hz = actual;
  self->duty = duty;
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pwm_duty_u16_obj, 1, 2,
                                           pwm_duty_u16);
static mp_obj_t pwm_deinit(mp_obj_t obj) {
  meos_pwm_t *self = pwm_object(obj);
  if (self->active) {
    pwm_check(self);
    int stop = io_pwm_stop(self->owner, self->pin);
    int release = io_pins_release(self->owner, &self->pin, 1);
    if (!release)
      self->active = false;
    mp_machine_error(stop < 0 ? stop : release);
  }
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(pwm_deinit_obj, pwm_deinit);
static const mp_rom_map_elem_t pwm_locals[] = {
    {MP_ROM_QSTR(MP_QSTR_freq), MP_ROM_PTR(&pwm_freq_obj)},
    {MP_ROM_QSTR(MP_QSTR_duty_u16), MP_ROM_PTR(&pwm_duty_u16_obj)},
    {MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&pwm_deinit_obj)},
};
static MP_DEFINE_CONST_DICT(pwm_locals_dict, pwm_locals);
MP_DEFINE_CONST_OBJ_TYPE(mp_machine_pwm_type, MP_QSTR_PWM, MP_TYPE_FLAG_NONE,
                         make_new, pwm_new, locals_dict, &pwm_locals_dict);
