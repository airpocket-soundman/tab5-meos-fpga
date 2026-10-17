/*
 * ESP-IDF v4.4.3 (esp_hw_support/port/esp32s3/spiram.c) initializes PSRAM from
 * call_start_cpu0() and maps it at the TOP of the external data window:
 *     [SOC_EXTRAM_DATA_HIGH - size, SOC_EXTRAM_DATA_HIGH)
 * (see esp_spiram_init_cache / esp_spiram_add_to_heapalloc).  With
 * CONFIG_SPIRAM_USE_MEMMAP nothing else touches it, so the whole range is ours.
 * g_spiram_ok (cpu_start.c) is false if the chip was not found
 * (CONFIG_SPIRAM_IGNORE_NOTFOUND).
 */
#include <m5os/hal_mem.h>
#include "soc/soc.h"
#include "sdkconfig.h"

#ifdef CONFIG_SPIRAM_BOOT_INIT
extern bool   g_spiram_ok;
extern size_t esp_spiram_get_size(void);
#endif

void hal_psram_region(uintptr_t *base, size_t *size)
{
#ifdef CONFIG_SPIRAM_BOOT_INIT
    if (!g_spiram_ok) {
        *base = 0;
        *size = 0;
        return;
    }
    size_t sz = esp_spiram_get_size();
    *size = sz;
    *base = (uintptr_t)SOC_EXTRAM_DATA_HIGH - sz;
#else
    *base = 0;                       /* PSRAM support is not built in (boards without PSRAM) */
    *size = 0;
#endif
}
