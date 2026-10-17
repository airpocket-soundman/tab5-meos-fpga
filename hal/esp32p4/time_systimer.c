/*
 * Monotonic clock from the ESP32-P4 SYSTIMER, counter 0.  Its clock is the 40 MHz crystal through a
 * fixed 2.5 divider = 16 MHz = 16 ticks per microsecond (esp_hw_support/port/esp32p4/systimer.c).
 * The CPU tick (irq.c) uses alarm 0 of the same counter; IDF's esp_timer owns alarm 2.
 */
#include <m5os/hal_time.h>
#include "hal/systimer_ll.h"
#include "soc/systimer_struct.h"

#define SYSTIMER_COUNTER_CLOCK  0
#define TICKS_PER_US            16u

void hal_time_init(void)
{
    systimer_ll_enable_clock(&SYSTIMER, true);                 /* idempotent: IDF has done it already */
    systimer_ll_enable_counter(&SYSTIMER, SYSTIMER_COUNTER_CLOCK, true);
}

uint64_t hal_time_ticks(void)                                  /* also used by the tick interrupt */
{
    systimer_ll_counter_snapshot(&SYSTIMER, SYSTIMER_COUNTER_CLOCK);
    while (!systimer_ll_is_counter_value_valid(&SYSTIMER, SYSTIMER_COUNTER_CLOCK)) { }
    uint32_t lo, lo2, hi;
    lo = systimer_ll_get_counter_value_low(&SYSTIMER, SYSTIMER_COUNTER_CLOCK);
    do {                                                       /* re-read: LO may roll over between the halves */
        lo2 = lo;
        hi = systimer_ll_get_counter_value_high(&SYSTIMER, SYSTIMER_COUNTER_CLOCK);
        lo = systimer_ll_get_counter_value_low(&SYSTIMER, SYSTIMER_COUNTER_CLOCK);
    } while (lo != lo2);
    return ((uint64_t)hi << 32) | lo;
}

uint64_t hal_time_now_us(void) { return hal_time_ticks() / TICKS_PER_US; }
