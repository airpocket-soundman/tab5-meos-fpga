/*
 * Monotonic clock from the ESP32-S3 SYSTIMER (64-bit, 16 ticks per us).
 * Only hal/systimer_ll.h register helpers are used (no esp_timer, which would
 * pull in a FreeRTOS task).  The 64-bit width means no wrap handling: it
 * would take ~36,000 years to overflow.
 */
#include <m5os/hal_time.h>
#include "hal/systimer_ll.h"
#include "soc/systimer_struct.h"

/* SDK 5.5 LL headers take the counter index directly. The S3 systimer is
 * XTAL / 2.5 = 16 MHz (esp_hw_support/port/esp32s3/systimer.c). */
#define SYSTIMER_COUNTER_CLOCK 0u
#define SYSTIMER_TICKS_PER_US 16u

void hal_time_init(void)
{
    /* IDF has typically enabled these already; both calls are idempotent. */
    systimer_ll_enable_clock(&SYSTIMER, true);
    systimer_ll_enable_counter(&SYSTIMER, SYSTIMER_COUNTER_CLOCK, true);
}

uint64_t hal_time_now_us(void)
{
    /* The counter is wider than a bus read: ask the hardware to latch a
     * snapshot, wait until it is valid, then read the two halves. */
    systimer_ll_counter_snapshot(&SYSTIMER, SYSTIMER_COUNTER_CLOCK);
    while (!systimer_ll_is_counter_value_valid(&SYSTIMER, SYSTIMER_COUNTER_CLOCK)) { }
    uint32_t lo = systimer_ll_get_counter_value_low(&SYSTIMER, SYSTIMER_COUNTER_CLOCK);
    uint32_t hi = systimer_ll_get_counter_value_high(&SYSTIMER, SYSTIMER_COUNTER_CLOCK);
    return (((uint64_t)hi << 32) | lo) / SYSTIMER_TICKS_PER_US;
}
