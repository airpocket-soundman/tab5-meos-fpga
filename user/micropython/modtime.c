/* Monotonic time only. No wall-clock/RTC semantics are invented. */
#include "py/mphal.h"
#include "py/runtime.h"
#include <m5os/hal_time.h>
#include <m5os/task.h>
#include <stdint.h>
#define TICKS_PERIOD (1u << 30)
#define TICKS_MASK (TICKS_PERIOD - 1u)
void meos_mp_poll(void);
static mp_obj_t time_sleep(mp_obj_t arg) {
  mp_float_t seconds = mp_obj_get_float(arg);
  if (!(seconds >= 0 && seconds <= 86400))
    mp_raise_ValueError(MP_ERROR_TEXT("sleep range is 0..86400 seconds"));
  mp_hal_delay_ms((mp_uint_t)(seconds * 1000));
  task_yield();
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(time_sleep_obj, time_sleep);
static mp_obj_t time_sleep_ms(mp_obj_t arg) {
  mp_int_t ms = mp_obj_get_int(arg);
  if (ms < 0)
    mp_raise_ValueError(MP_ERROR_TEXT("negative delay"));
  mp_hal_delay_ms(ms);
  task_yield();
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(time_sleep_ms_obj, time_sleep_ms);
static mp_obj_t time_sleep_us(mp_obj_t arg) {
  mp_int_t us = mp_obj_get_int(arg);
  if (us < 0)
    mp_raise_ValueError(MP_ERROR_TEXT("negative delay"));
  if (us >= 1000)
    mp_hal_delay_ms(us / 1000);
  mp_hal_delay_us(us % 1000);
  meos_mp_poll();
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(time_sleep_us_obj, time_sleep_us);
static mp_obj_t time_ticks_ms(void) {
  return mp_obj_new_int_from_uint((hal_time_now_us() / 1000) & TICKS_MASK);
}
static MP_DEFINE_CONST_FUN_OBJ_0(time_ticks_ms_obj, time_ticks_ms);
static mp_obj_t time_ticks_us(void) {
  return mp_obj_new_int_from_uint(hal_time_now_us() & TICKS_MASK);
}
static MP_DEFINE_CONST_FUN_OBJ_0(time_ticks_us_obj, time_ticks_us);
static mp_int_t tick_value(mp_obj_t arg) {
  mp_int_t value = mp_obj_get_int(arg);
  if (value < 0 || value >= TICKS_PERIOD)
    mp_raise_ValueError(MP_ERROR_TEXT("tick outside period"));
  return value;
}
static mp_obj_t time_ticks_diff(mp_obj_t a, mp_obj_t b) {
  mp_int_t first = tick_value(a), second = tick_value(b);
  return mp_obj_new_int(
      (mp_int_t)(((uint32_t)first - (uint32_t)second + TICKS_PERIOD / 2) &
                 TICKS_MASK) -
      (mp_int_t)(TICKS_PERIOD / 2));
}
static MP_DEFINE_CONST_FUN_OBJ_2(time_ticks_diff_obj, time_ticks_diff);
static mp_obj_t time_ticks_add(mp_obj_t a, mp_obj_t b) {
  mp_int_t value = tick_value(a), delta = mp_obj_get_int(b);
  if (delta < -(mp_int_t)(TICKS_PERIOD / 2) ||
      delta >= (mp_int_t)(TICKS_PERIOD / 2))
    mp_raise_ValueError(MP_ERROR_TEXT("delta outside half period"));
  return mp_obj_new_int_from_uint(((uint32_t)value + (uint32_t)delta) &
                                  TICKS_MASK);
}
static MP_DEFINE_CONST_FUN_OBJ_2(time_ticks_add_obj, time_ticks_add);
static const mp_rom_map_elem_t time_globals[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_time)},
    {MP_ROM_QSTR(MP_QSTR_sleep), MP_ROM_PTR(&time_sleep_obj)},
    {MP_ROM_QSTR(MP_QSTR_sleep_ms), MP_ROM_PTR(&time_sleep_ms_obj)},
    {MP_ROM_QSTR(MP_QSTR_sleep_us), MP_ROM_PTR(&time_sleep_us_obj)},
    {MP_ROM_QSTR(MP_QSTR_ticks_ms), MP_ROM_PTR(&time_ticks_ms_obj)},
    {MP_ROM_QSTR(MP_QSTR_ticks_us), MP_ROM_PTR(&time_ticks_us_obj)},
    {MP_ROM_QSTR(MP_QSTR_ticks_diff), MP_ROM_PTR(&time_ticks_diff_obj)},
    {MP_ROM_QSTR(MP_QSTR_ticks_add), MP_ROM_PTR(&time_ticks_add_obj)},
};
static MP_DEFINE_CONST_DICT(time_globals_dict, time_globals);
const mp_obj_module_t mp_module_time = {
    .base = {&mp_type_module}, .globals = (mp_obj_dict_t *)&time_globals_dict};
MP_REGISTER_MODULE(MP_QSTR_time, mp_module_time);
