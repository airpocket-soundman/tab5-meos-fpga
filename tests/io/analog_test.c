#include <assert.h>
#include <m5os/errno.h>
#include <m5os/hal_io_analog.h>
#include <stdio.h>
#include <string.h>
static unsigned irq_depth, hardware_calls;
uint32_t arch_irq_disable(void) {
    irq_depth++;
    return 8;
}
void arch_irq_restore(uint32_t f) {
    (void)f;
    assert(irq_depth);
    irq_depth--;
}
static void pwm_native_set(int p, unsigned b, uint32_t d, unsigned duty) {
    (void)p;
    assert(irq_depth && b >= 1 && b <= 20 && d >= 256 && d <= 0x3ffff && duty <= 1000);
    hardware_calls++;
}
static void pwm_native_stop(int p) {
    (void)p;
    assert(irq_depth);
    hardware_calls++;
}
static int adc_result;
static int adc_native_read(unsigned u, unsigned c, unsigned a, uint16_t *raw) {
    assert(irq_depth && u >= 1 && u <= 2 && c < 8 && a < 4);
    hardware_calls++;
    if (!adc_result)
        *raw = 1234;
    return adc_result;
}
#define MEOS_ANALOG_HOST_TEST 1
#include "../../hal/esp32p4/io_analog.c"
int main(void) {
    unsigned unit, channel;
    uint16_t raw = 99;
    uint32_t hz;
    for (int pin = 0; pin < 55; pin++) {
        int rc = hal_io_adc_channel(pin, &unit, &channel);
        if (pin >= 16 && pin <= 23)
            assert(!rc && unit == 1 && channel == (unsigned)pin - 16);
        else if (pin >= 49)
            assert(!rc && unit == 2 && channel == (unsigned)pin - 49);
        else
            assert(rc == ERR_NOSYS);
    }
    assert(hal_io_adc_channel(16, NULL, &channel) == ERR_INVAL);
    assert(hal_io_adc_read_raw(16, 4, &raw) == ERR_INVAL && !hardware_calls);
    assert(hal_io_adc_read_raw(31, 0, &raw) == ERR_NOSYS && !hardware_calls);
    assert(hal_io_adc_read_raw(16, 0, &raw) == 0 && raw == 1234);
    adc_result = ERR_TIMEOUT;
    raw = 99;
    assert(hal_io_adc_read_raw(54, 3, &raw) == ERR_TIMEOUT && raw == 99 && !irq_depth);
    unsigned bits;
    uint32_t div, actual;
    for (uint32_t f = 1; f <= 40000000; f = f < 100 ? f + 1 : f * 2) {
        assert(pwm_parameters(f, &bits, &div, &actual));
        assert(bits >= 1 && bits <= 20 && div >= 256 && div <= 0x3ffff && actual > 0);
    }
    assert(!pwm_parameters(0, &bits, &div, &actual) &&
           !pwm_parameters(40000001, &bits, &div, &actual));
    assert(pwm_parameters(50, &bits, &div, &actual) && actual == 50);
    assert(hal_io_pwm_set(16, 50, 1001, &hz) == ERR_INVAL);
    assert(hal_io_pwm_set(16, 50, 0, &hz) == 0 && hz == 50);
    unsigned before = hardware_calls;
    assert(hal_io_pwm_set(17, 1000, 500, &hz) == ERR_BUSY && hardware_calls == before);
    assert(hal_io_pwm_stop(17) == ERR_NODEV && hardware_calls == before);
    assert(hal_io_pwm_set(16, 1000, 1000, &hz) == 0 && hz >= 998 && hz <= 1002);
    assert(hal_io_pwm_stop(16) == 0);
    assert(hal_io_pwm_set(17, 40000000, 500, &hz) == 0 && hz == 40000000);
    assert(hal_io_pwm_stop(17) == 0 && !irq_depth);
    puts("PASS analog mapping/invalid inputs/ADC timeout propagation/PWM divider/resource "
         "ownership");
}
