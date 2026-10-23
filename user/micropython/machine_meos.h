#pragma once
#include "py/runtime.h"
#include <m5os/io.h>
io_owner_t mp_machine_owner(void);
void mp_machine_error(int error);
void mp_machine_owner_check(io_owner_t owner);
void meos_mp_poll(void);
extern const mp_obj_type_t mp_machine_pwm_type;
extern const mp_obj_type_t mp_machine_uart_type;
extern const mp_obj_type_t mp_machine_soft_i2c_type;
extern const mp_obj_type_t mp_machine_soft_spi_type;
