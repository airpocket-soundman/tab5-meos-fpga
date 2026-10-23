#pragma once
#include <m5os/types.h>
/* Caller must own and validate the board pin before invoking this raw HAL.
 * ADC attenuation codes:0=0dB,1=2.5dB,2=6dB,3=12dB. Raw12-bit, no voltage calibration.
 * PWM reserves LEDC timer1/channel1; one simultaneous pin. Camera uses0/0.
 * Task context only. No RTOS driver entry points. */
int hal_io_adc_channel(int pin, unsigned *unit, unsigned *channel);
int hal_io_adc_read_raw(int pin, unsigned attenuation, uint16_t *raw);
int hal_io_pwm_set(int pin, uint32_t frequency_hz, unsigned duty_permille, uint32_t *actual_hz);
int hal_io_pwm_stop(int pin);
