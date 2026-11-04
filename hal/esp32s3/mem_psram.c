/* CoreS3 keeps the pinned IDF4.4 startup contract. IDF5.x uses public
 * PSRAM APIs and queries the MMU's actual DATA mapping instead; never import
 * IDF4 private symbols into a modern build. Both paths leave absent RAM alone. */
#include <m5os/hal_mem.h>
#include "sdkconfig.h"
#include "soc/soc.h"
#include "esp_idf_version.h"
#if ESP_IDF_VERSION_MAJOR >= 5 && CONFIG_SPIRAM_BOOT_INIT && CONFIG_SPIRAM_USE_MEMMAP
#include "esp_psram.h"
#include "esp_private/esp_mmu_map_private.h"
#include "esp_mmu_map.h"
#endif

void hal_psram_region(uintptr_t *base, size_t *size)
{
    *base=0;*size=0;
#if ESP_IDF_VERSION_MAJOR < 5
#if CONFIG_SPIRAM_BOOT_INIT
    /* IDF4.4.3 maps MEMMAP PSRAM at the TOP of the external DATA window
     * (esp_hw_support/port/esp32s3/spiram.c). Preserve that pinned port ABI. */
    extern bool g_spiram_ok;
    extern size_t esp_spiram_get_size(void);
    if(!g_spiram_ok)return;
    size_t bytes=esp_spiram_get_size();
    *base=(uintptr_t)SOC_EXTRAM_DATA_HIGH-bytes;*size=bytes;
#endif
#else
#if CONFIG_SPIRAM_BOOT_INIT && CONFIG_SPIRAM_USE_MEMMAP && !CONFIG_SPIRAM_FETCH_INSTRUCTIONS && !CONFIG_SPIRAM_RODATA
    if(!esp_psram_is_initialized())return;
    size_t bytes=esp_psram_get_size();
    void *mapped=NULL;
    if(!bytes||esp_mmu_paddr_to_vaddr(0,MMU_TARGET_PSRAM0,MMU_VADDR_DATA,&mapped)!=ESP_OK||!mapped)return;
    uintptr_t begin=(uintptr_t)mapped;
    if(begin<SOC_EXTRAM_DATA_LOW||begin>=SOC_EXTRAM_DATA_HIGH||bytes>SOC_EXTRAM_DATA_HIGH-begin)return;
    /* Physical RAM may exceed the available VA window. Validate each page and
     * the final byte before handing any of it to the private MeOS allocator. */
    for(size_t offset=0;offset<bytes;){
        void *at=NULL;
        if(esp_mmu_paddr_to_vaddr((uint32_t)offset,MMU_TARGET_PSRAM0,MMU_VADDR_DATA,&at)!=ESP_OK||
           (uintptr_t)at!=begin+offset)return;
        if(bytes-offset<=CONFIG_MMU_PAGE_SIZE)break;
        offset+=CONFIG_MMU_PAGE_SIZE;
    }
    void *last=NULL;
    if(esp_mmu_paddr_to_vaddr((uint32_t)(bytes-1),MMU_TARGET_PSRAM0,MMU_VADDR_DATA,&last)!=ESP_OK||
       (uintptr_t)last!=begin+bytes-1)return;
    uintptr_t used_end=begin;
#if CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
    extern uint8_t _ext_ram_bss_end;
    if((uintptr_t)&_ext_ram_bss_end>used_end)used_end=(uintptr_t)&_ext_ram_bss_end;
#endif
#if CONFIG_SPIRAM_ALLOW_NOINIT_SEG_EXTERNAL_MEMORY
    extern uint8_t _ext_ram_noinit_end;
    if((uintptr_t)&_ext_ram_noinit_end>used_end)used_end=(uintptr_t)&_ext_ram_noinit_end;
#endif
    used_end=(used_end+63u)&~(uintptr_t)63u;
    if(used_end<begin||used_end-begin>=bytes)return;
    *base=used_end;*size=bytes-(used_end-begin);
#endif
#endif
}
