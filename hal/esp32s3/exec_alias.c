/*
 * Verified in ESP-IDF v4.4.3 soc/esp32s3/include/soc/soc.h:
 *   SOC_DIRAM_DRAM_LOW..HIGH  = 0x3FC88000..0x3FCF0000   (data-bus view)
 *   SOC_DIRAM_IRAM_LOW..HIGH  = 0x40378000..0x403E0000   (instruction-bus view)
 *   SOC_I_D_OFFSET            = IRAM_LOW - DRAM_LOW = 0x6F0000
 * so   iram_address = dram_address + SOC_I_D_OFFSET   (MAP_DRAM_TO_IRAM).
 * Nothing in this range is cached and, as long as IDF's optional memory
 * protection is off (it is), it is executable.
 */
#include <m5os/hal_exec.h>
#include "soc/soc.h"

uintptr_t hal_exec_alias(const void *p, size_t len)
{
    uintptr_t a = (uintptr_t)p;
    if (a < SOC_DIRAM_DRAM_LOW || len > SOC_DIRAM_DRAM_HIGH || a > SOC_DIRAM_DRAM_HIGH - len) return 0;
    return a + SOC_I_D_OFFSET;
}

void hal_exec_sync(void *p, size_t len)
{
    (void)p; (void)len;
    /* memw: finish pending stores; isync: drop the instruction prefetch queue. */
    __asm__ volatile ("memw\n\tisync" ::: "memory");
}
