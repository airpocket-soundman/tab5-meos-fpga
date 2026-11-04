/*
 * RISC-V: there is a single address space, so code copied into RAM is executed at the very
 * address it was written to (no separate instruction-bus alias as on the ESP32-S3).  Internal
 * RAM (L2MEM) is executable only in the explicit SDK RWX-IDRAM build. With
 * locked SDK RX/RW splitting, ordinary heap memory is non-executable.
 */
#include <m5os/hal_exec.h>
#include "soc/soc.h"
#include "sdkconfig.h"
#include "esp_cache.h"
#include "hal/cache_hal.h"
#include "hal/cache_ll.h"

uintptr_t hal_exec_alias(const void *p, size_t len)
{
    uintptr_t a = (uintptr_t)p;
#if CONFIG_ESP_SYSTEM_PMP_IDRAM_SPLIT
    (void)a;(void)len;return 0;
#else
    if (!p || !len || a < SOC_DRAM_LOW || len > SOC_DRAM_HIGH || a > SOC_DRAM_HIGH - len) return 0;
    return a;
#endif
}

bool hal_exec_sync(void *p, size_t len)
{
    if(!hal_exec_alias(p,len))return false;
    /* Loader gives this image isolated128-byte cache lines. IDF M2C forbids
     * UNALIGNED and invalidates all cache types, so never round across another
     * allocation's dirty data. Reject unsupported future line sizes. */
    uint32_t dl=cache_hal_get_cache_line_size(CACHE_LL_LEVEL_INT_MEM,CACHE_TYPE_DATA);
    uint32_t il=cache_hal_get_cache_line_size(CACHE_LL_LEVEL_INT_MEM,CACHE_TYPE_INSTRUCTION);
    if(!dl||!il||dl>128||il>128||128%dl||128%il||((uintptr_t)p%128))return false;
    size_t aligned=(len+127u)&~(size_t)127u;
    if(!hal_exec_alias(p,aligned))return false;
    if(esp_cache_msync(p,aligned,ESP_CACHE_MSYNC_FLAG_DIR_C2M|ESP_CACHE_MSYNC_FLAG_TYPE_DATA)!=ESP_OK)return false;
    if(esp_cache_msync(p,aligned,ESP_CACHE_MSYNC_FLAG_DIR_M2C|ESP_CACHE_MSYNC_FLAG_TYPE_INST)!=ESP_OK)return false;
    __asm__ volatile ("fence.i" ::: "memory");       /* make stores visible to instruction fetch */
    return true;
}
