/*
 * RISC-V: there is a single address space, so code copied into RAM is executed at the very
 * address it was written to (no separate instruction-bus alias as on the ESP32-S3).  Internal
 * RAM (L2MEM) is executable; PSRAM and everything else is refused.
 */
#include <m5os/hal_exec.h>
#include "soc/soc.h"

uintptr_t hal_exec_alias(const void *p, size_t len)
{
    uintptr_t a = (uintptr_t)p;
    if (a < SOC_DRAM_LOW || len > SOC_DRAM_HIGH || a > SOC_DRAM_HIGH - len) return 0;
    return a;
}

void hal_exec_sync(void *p, size_t len)
{
    (void)p; (void)len;
    __asm__ volatile ("fence.i" ::: "memory");       /* make stores visible to instruction fetch */
}
