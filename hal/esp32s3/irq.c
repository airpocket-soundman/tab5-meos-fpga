/*
 * Interrupt masking, idle and the periodic tick for ESP32-S3 (Xtensa).
 * Only CPU-internal registers are used (INTENABLE, CCOUNT, CCOMPARE1,
 * VECBASE): no IDF driver, no esp_intr_alloc (which is FreeRTOS-aware).
 */
#include <m5os/arch.h>
#include "esp_private/esp_clk.h"      /* esp_clk_cpu_freq(): register read only */

#define TICK_TIMER_INT      15u       /* CCOMPARE1 = interrupt 15, level 3 (core-isa.h) */
#define KERNEL_INTLEVEL     3         /* highest "medium" level (XCHAL_EXCM_LEVEL) */
#define PS_INTLEVEL_MASK    0xFu
#define HZ_PER_MHZ          1000000u

extern char m5_vector_table[];

static void (*s_tick_handler)(void);
static uint32_t s_period_cycles;

uint32_t arch_irq_disable(void)
{
    uint32_t ps;
    /* RSIL: ps <- PS ; PS.INTLEVEL <- 3.  Atomic read-and-raise. */
    __asm__ volatile ("rsil %0, %1" : "=a"(ps) : "i"(KERNEL_INTLEVEL) : "memory");
    return ps;
}

void arch_irq_restore(uint32_t ps)
{
    /* Only INTLEVEL is restored (writing the whole PS would also write
     * CALLINC/OWB, which describe the *current* call, not the saved one). */
    switch (ps & PS_INTLEVEL_MASK) {
    case 0:  __asm__ volatile ("rsil a2, 0" ::: "a2", "memory"); break;
    case 1:  __asm__ volatile ("rsil a2, 1" ::: "a2", "memory"); break;
    case 2:  __asm__ volatile ("rsil a2, 2" ::: "a2", "memory"); break;
    default: break;                       /* was already masked: stay masked */
    }
}

bool arch_irq_enabled(void)
{
    uint32_t ps;
    __asm__ volatile ("rsr %0, ps" : "=a"(ps));
    return (ps & PS_INTLEVEL_MASK) == 0;
}

void arch_cpu_idle(void)
{
    __asm__ volatile ("waiti 0" ::: "memory");   /* INTLEVEL <- 0, sleep until interrupt */
}

static inline uint32_t read_ccount(void)
{
    uint32_t v;
    __asm__ volatile ("rsr %0, ccount" : "=a"(v));
    return v;
}

static inline void write_ccompare1(uint32_t v)
{
    /* Writing CCOMPAREn also clears a pending timer interrupt. */
    __asm__ volatile ("wsr %0, ccompare1; esync" :: "a"(v) : "memory");
}

/* Called from m5_level3_entry (vectors.S) with PS = INTLEVEL 3, EXCM clear. */
void m5_level3_dispatch(void)
{
    uint32_t pending;
    __asm__ volatile ("rsr %0, interrupt" : "=a"(pending));

    if (pending & (1u << TICK_TIMER_INT)) {
        /* Re-arm first: keeps the period stable regardless of how long the
         * scheduler takes.  If we are already late, restart from now. */
        uint32_t next;
        __asm__ volatile ("rsr %0, ccompare1" : "=a"(next));
        next += s_period_cycles;
        if ((int32_t)(next - read_ccount()) <= 0) {
            next = read_ccount() + s_period_cycles;
        }
        write_ccompare1(next);

        if (s_tick_handler) {
            s_tick_handler();      /* may switch tasks; returns when we run again */
        }
    }
}

void arch_tick_init(uint32_t period_us, void (*handler)(void))
{
    uint32_t flags = arch_irq_disable();

    uint32_t cycles_per_us = (uint32_t)esp_clk_cpu_freq() / HZ_PER_MHZ;
    s_period_cycles = period_us * cycles_per_us;
    s_tick_handler  = handler;

    /* Switch from IDF's vector table to ours (see vectors.S). */
    __asm__ volatile ("wsr %0, vecbase; rsync" :: "a"(m5_vector_table) : "memory");

    write_ccompare1(read_ccount() + s_period_cycles);

    uint32_t en;
    __asm__ volatile ("rsr %0, intenable" : "=a"(en));
    en |= 1u << TICK_TIMER_INT;
    __asm__ volatile ("wsr %0, intenable; rsync" :: "a"(en) : "memory");

    arch_irq_restore(flags);
}
