/* SPDX-License-Identifier: Apache-2.0
 * Native ADC/PWM register sequencing follows Espressif ESP-IDF v5.4.4 HAL/LL.
 * Copyright Espressif Systems (Shanghai) CO LTD 2023-2025; MeOS adaptation.
 */
#include <m5os/arch.h>
#include <m5os/errno.h>
#include <m5os/hal_io_analog.h>
#include <m5os/hal_time.h>
#ifndef MEOS_ANALOG_HOST_TEST
#include "esp_rom_gpio.h"
#include "esp_rom_regi2c.h"
#include "esp_rom_sys.h"
#include "hal/adc_ll.h"
#include "hal/gpio_ll.h"
#include "hal/ledc_ll.h"
#include "soc/gpio_sig_map.h"
#include "soc/pmu_reg.h"
#endif
#define IO_PWM_CLOCK 80000000ULL
#define IO_PWM_TIMER LEDC_TIMER_1
#define IO_PWM_CHANNEL LEDC_CHANNEL_1
static int s_pwm_pin = -1;
int hal_io_adc_channel(int pin, unsigned *unit, unsigned *channel) {
    if (!unit || !channel)
        return ERR_INVAL;
    if (pin >= 16 && pin <= 23) {
        *unit = 1;
        *channel = (unsigned)(pin - 16);
        return 0;
    }
    if (pin >= 49 && pin <= 54) {
        *unit = 2;
        *channel = (unsigned)(pin - 49);
        return 0;
    }
    return ERR_NOSYS;
}
static bool pwm_parameters(uint32_t hz, unsigned *bits, uint32_t *divider, uint32_t *actual) {
    if (!hz || hz > IO_PWM_CLOCK / 2)
        return false;
    for (unsigned b = 20; b; b--) {
        uint64_t period = (uint64_t)hz * (1u << b);
        uint64_t div = (IO_PWM_CLOCK * 256 + period / 2) / period;
        if (div < 256 || div > 0x3ffff)
            continue;
        uint64_t denominator = div * (1u << b);
        *bits = b;
        *divider = (uint32_t)div;
        *actual = (uint32_t)((IO_PWM_CLOCK * 256 + denominator / 2) / denominator);
        return true;
    }
    return false;
}
static void pwm_native_set(int pin, unsigned bits, uint32_t divider, unsigned duty);
static void pwm_native_stop(int pin);
static int adc_native_read(unsigned unit, unsigned channel, unsigned attenuation, uint16_t *raw);
int hal_io_adc_read_raw(int pin, unsigned attenuation, uint16_t *raw) {
    unsigned unit, channel;
    if (!raw || attenuation > 3)
        return ERR_INVAL;
    int rc = hal_io_adc_channel(pin, &unit, &channel);
    if (rc)
        return rc;
    uint32_t irq = arch_irq_disable();
    rc = adc_native_read(unit, channel, attenuation, raw);
    arch_irq_restore(irq);
    return rc;
}
#ifndef MEOS_ANALOG_HOST_TEST
static void pwm_native_set(int pin, unsigned bits, uint32_t divider, unsigned duty) {
    int __DECLARE_RCC_ATOMIC_ENV = 0;
    ledc_ll_enable_bus_clock(true);
    ledc_ll_enable_clock(&LEDC, true);
    ledc_ll_set_slow_clk_sel(&LEDC, LEDC_SLOW_CLK_PLL_DIV);
    ledc_ll_timer_pause(&LEDC, LEDC_LOW_SPEED_MODE, IO_PWM_TIMER);
    ledc_ll_set_clock_divider(&LEDC, LEDC_LOW_SPEED_MODE, IO_PWM_TIMER, divider);
    ledc_ll_set_duty_resolution(&LEDC, LEDC_LOW_SPEED_MODE, IO_PWM_TIMER, bits);
    ledc_ll_ls_timer_update(&LEDC, LEDC_LOW_SPEED_MODE, IO_PWM_TIMER);
    ledc_ll_timer_rst(&LEDC, LEDC_LOW_SPEED_MODE, IO_PWM_TIMER);
    ledc_ll_bind_channel_timer(&LEDC, LEDC_LOW_SPEED_MODE, IO_PWM_CHANNEL, IO_PWM_TIMER);
    ledc_ll_set_hpoint(&LEDC, LEDC_LOW_SPEED_MODE, IO_PWM_CHANNEL, 0);
    uint32_t ticks = (uint32_t)(((uint64_t)(1u << bits) * duty + 500) / 1000);
    ledc_ll_set_duty_int_part(&LEDC, LEDC_LOW_SPEED_MODE, IO_PWM_CHANNEL, ticks);
    ledc_ll_set_fade_param_range(&LEDC, LEDC_LOW_SPEED_MODE, IO_PWM_CHANNEL, 0, 1, 1, 0, 1);
    ledc_ll_set_range_number(&LEDC, LEDC_LOW_SPEED_MODE, IO_PWM_CHANNEL, 1);
    ledc_ll_set_duty_start(&LEDC, LEDC_LOW_SPEED_MODE, IO_PWM_CHANNEL);
    ledc_ll_set_sig_out_en(&LEDC, LEDC_LOW_SPEED_MODE, IO_PWM_CHANNEL, true);
    ledc_ll_ls_channel_update(&LEDC, LEDC_LOW_SPEED_MODE, IO_PWM_CHANNEL);
    gpio_ll_func_sel(&GPIO, pin, PIN_FUNC_GPIO);
    gpio_ll_output_enable(&GPIO, pin);
    esp_rom_gpio_connect_out_signal(pin, LEDC_LS_SIG_OUT_PAD_OUT1_IDX, false, false);
    ledc_ll_timer_resume(&LEDC, LEDC_LOW_SPEED_MODE, IO_PWM_TIMER);
    ledc_ll_ls_timer_update(&LEDC, LEDC_LOW_SPEED_MODE, IO_PWM_TIMER);
}
static void pwm_native_stop(int pin) {
    ledc_ll_set_sig_out_en(&LEDC, LEDC_LOW_SPEED_MODE, IO_PWM_CHANNEL, false);
    ledc_ll_ls_channel_update(&LEDC, LEDC_LOW_SPEED_MODE, IO_PWM_CHANNEL);
    ledc_ll_timer_pause(&LEDC, LEDC_LOW_SPEED_MODE, IO_PWM_TIMER);
    esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, false, false);
    gpio_ll_set_level(&GPIO, pin, 0);
}
#define ADC_READ_MASK(reg)                                                                         \
    esp_rom_regi2c_read_mask(I2C_SAR_ADC, I2C_SAR_ADC_HOSTID, reg, reg##_MSB, reg##_LSB)
#define ADC_WRITE_MASK(reg, value)                                                                 \
    esp_rom_regi2c_write_mask(I2C_SAR_ADC, I2C_SAR_ADC_HOSTID, reg, reg##_MSB, reg##_LSB, value)
static int adc_native_read(unsigned unit, unsigned channel, unsigned attenuation, uint16_t *raw) {
    int __DECLARE_RCC_ATOMIC_ENV = 0;
    int pin = unit == 1 ? (int)channel + 16 : (int)channel + 49;
    gpio_ll_func_sel(&GPIO, pin, PIN_FUNC_GPIO);
    gpio_ll_output_disable(&GPIO, pin);
    gpio_ll_input_disable(&GPIO, pin);
    gpio_ll_pullup_dis(&GPIO, pin);
    gpio_ll_pulldown_dis(&GPIO, pin);
    uint32_t clock1 = HP_SYS_CLKRST.soc_clk_ctrl2.val, clock2 = HP_SYS_CLKRST.peri_clk_ctrl23.val;
    uint32_t lpclock = LPPERI.clk_en.val;
    adc_ll_enable_bus_clock(true);
    LPPERI.clk_en.ck_en_lp_adc = 1;
    uint32_t ctrl = ADC.ctrl_reg.val, power = LP_ADC.force_wpd_sar.val;
    uint32_t reader = unit == 1 ? LP_ADC.reader1_ctrl.val : LP_ADC.reader2_ctrl.val;
    uint32_t mux = unit == 1 ? LP_ADC.meas1_mux.val : LP_ADC.meas2_mux.val;
    uint32_t meas = unit == 1 ? LP_ADC.meas1_ctrl2.val : LP_ADC.meas2_ctrl2.val;
    uint32_t atten = unit == 1 ? LP_ADC.atten1.val : LP_ADC.atten2.val;
    uint32_t pmu = REG_READ(PMU_RF_PWC_REG);
    REG_SET_BIT(PMU_RF_PWC_REG, PMU_XPD_PERIF_I2C);
    uint8_t initial_low = unit == 1 ? ADC_READ_MASK(ADC_SAR1_INITIAL_CODE_LOW_ADDR)
                                    : ADC_READ_MASK(ADC_SAR2_INITIAL_CODE_LOW_ADDR);
    uint8_t initial_high = unit == 1 ? ADC_READ_MASK(ADC_SAR1_INITIAL_CODE_HIGH_ADDR)
                                     : ADC_READ_MASK(ADC_SAR2_INITIAL_CODE_HIGH_ADDR);
    uint8_t sample = unit == 1 ? ADC_READ_MASK(ADC_SAR1_SAMPLE_CYCLE_ADDR)
                               : ADC_READ_MASK(ADC_SAR2_SAMPLE_CYCLE_ADDR);
    /* Nominal init code follows IDF boot entropy setup; not eFuse calibration. */
    if (unit == 1) {
        ADC_WRITE_MASK(ADC_SAR1_INITIAL_CODE_LOW_ADDR, 2166 & 255);
        ADC_WRITE_MASK(ADC_SAR1_INITIAL_CODE_HIGH_ADDR, 2166 >> 8);
        ADC_WRITE_MASK(ADC_SAR1_SAMPLE_CYCLE_ADDR, 2);
    } else {
        ADC_WRITE_MASK(ADC_SAR2_INITIAL_CODE_LOW_ADDR, 2166 & 255);
        ADC_WRITE_MASK(ADC_SAR2_INITIAL_CODE_HIGH_ADDR, 2166 >> 8);
        ADC_WRITE_MASK(ADC_SAR2_SAMPLE_CYCLE_ADDR, 2);
    }
    adc_unit_t index = unit == 1 ? ADC_UNIT_1 : ADC_UNIT_2;
    adc_ll_set_power_manage(index, ADC_LL_POWER_SW_ON);
    adc_ll_set_sar_clk_div(index, ADC_LL_SAR_CLK_DIV_DEFAULT(index));
    adc_oneshot_ll_output_invert(index, false);
    adc_oneshot_ll_set_atten(index, (adc_channel_t)channel, (adc_atten_t)attenuation);
    adc_oneshot_ll_set_channel(index, (adc_channel_t)channel);
    adc_ll_set_controller(index, ADC_LL_CTRL_RTC);
    esp_rom_delay_us(20);
    uint32_t event = unit == 1 ? ADC_LL_EVENT_ADC1_ONESHOT_DONE : ADC_LL_EVENT_ADC2_ONESHOT_DONE;
    adc_oneshot_ll_start(index);
    uint64_t deadline = hal_time_now_us() + 1000;
    while (!adc_oneshot_ll_get_event(event) && hal_time_now_us() < deadline) {
    }
    int result = ERR_TIMEOUT;
    if (adc_oneshot_ll_get_event(event)) {
        uint32_t value = adc_oneshot_ll_get_raw_result(index);
        if (value <= 4095) {
            *raw = (uint16_t)value;
            result = 0;
        } else
            result = ERR_IO;
    }
    /* Stop and restore only the selected unit, plus common power/clock fields.
     * IRQ exclusion also serializes with SSH's temporary ADC entropy use. */
    if (unit == 1) {
        LP_ADC.meas1_ctrl2.meas1_start_sar = 0;
        LP_ADC.reader1_ctrl.val = reader;
        LP_ADC.meas1_mux.val = mux;
        LP_ADC.meas1_ctrl2.val = meas;
        LP_ADC.atten1.val = atten;
        ADC_WRITE_MASK(ADC_SAR1_INITIAL_CODE_LOW_ADDR, initial_low);
        ADC_WRITE_MASK(ADC_SAR1_INITIAL_CODE_HIGH_ADDR, initial_high);
        ADC_WRITE_MASK(ADC_SAR1_SAMPLE_CYCLE_ADDR, sample);
    } else {
        LP_ADC.meas2_ctrl2.meas2_start_sar = 0;
        LP_ADC.reader2_ctrl.val = reader;
        LP_ADC.meas2_mux.val = mux;
        LP_ADC.meas2_ctrl2.val = meas;
        LP_ADC.atten2.val = atten;
        ADC_WRITE_MASK(ADC_SAR2_INITIAL_CODE_LOW_ADDR, initial_low);
        ADC_WRITE_MASK(ADC_SAR2_INITIAL_CODE_HIGH_ADDR, initial_high);
        ADC_WRITE_MASK(ADC_SAR2_SAMPLE_CYCLE_ADDR, sample);
    }
    LP_ADC.force_wpd_sar.val = power;
    ADC.ctrl_reg.val = ctrl;
    REG_WRITE(PMU_RF_PWC_REG, pmu);
    LPPERI.clk_en.val = lpclock;
    HP_SYS_CLKRST.peri_clk_ctrl23.val = clock2;
    HP_SYS_CLKRST.soc_clk_ctrl2.val = clock1;
    return result;
}
#endif
int hal_io_pwm_set(int pin, uint32_t hz, unsigned duty, uint32_t *actual_hz) {
    unsigned bits;
    uint32_t divider, actual;
    if (pin < 0 || pin > 54 || duty > 1000 || !actual_hz ||
        !pwm_parameters(hz, &bits, &divider, &actual))
        return ERR_INVAL;
    uint32_t irq = arch_irq_disable();
    if (s_pwm_pin >= 0 && s_pwm_pin != pin) {
        arch_irq_restore(irq);
        return ERR_BUSY;
    }
    pwm_native_set(pin, bits, divider, duty);
    s_pwm_pin = pin;
    *actual_hz = actual;
    arch_irq_restore(irq);
    return 0;
}
int hal_io_pwm_stop(int pin) {
    uint32_t irq = arch_irq_disable();
    if (s_pwm_pin != pin || pin < 0) {
        arch_irq_restore(irq);
        return ERR_NODEV;
    }
    pwm_native_stop(pin);
    s_pwm_pin = -1;
    arch_irq_restore(irq);
    return 0;
}
