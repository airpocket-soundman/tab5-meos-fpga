#include <assert.h>
#include <stdio.h>
#include <m5os/hal_mem.h>
#include "esp_idf_version.h"
static bool initialized=true;
bool g_spiram_ok=true;
static size_t bytes=8u*1024u*1024u;
static uintptr_t mapping=0x3c280000u;
uintptr_t test_psram_low=0x3c000000u,test_psram_high=0x3e000000u;
uint8_t _ext_ram_bss_end,_ext_ram_noinit_end;
static int map_error;static bool gap;static unsigned queries;
bool esp_psram_is_initialized(void){return initialized;}
size_t esp_psram_get_size(void){return bytes;}
size_t esp_spiram_get_size(void){return bytes;}
int esp_mmu_paddr_to_vaddr(unsigned p,int target,int type,void**out){
 assert(!target&&!type);queries++;
 if(map_error||(gap&&p>=65536u))return 1;
 *out=(void*)(mapping+p);return 0;
}
static void expect(uintptr_t base,size_t size){uintptr_t b;size_t s;hal_psram_region(&b,&s);assert(b==base&&s==size);}
int main(void){
#if ESP_IDF_VERSION_MAJOR < 5
#if CONFIG_SPIRAM_BOOT_INIT
 expect(test_psram_high-bytes,bytes);assert(!queries);
 g_spiram_ok=false;expect(0,0);
#else
 expect(0,0);assert(!queries);
#endif
#else
#if !CONFIG_SPIRAM_BOOT_INIT || !CONFIG_SPIRAM_USE_MEMMAP || CONFIG_SPIRAM_FETCH_INSTRUCTIONS || CONFIG_SPIRAM_RODATA
 expect(0,0);assert(!queries);
#else
#if CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY || CONFIG_SPIRAM_ALLOW_NOINIT_SEG_EXTERNAL_MEMORY
 uintptr_t end=(uintptr_t)&_ext_ram_bss_end;
#if CONFIG_SPIRAM_ALLOW_NOINIT_SEG_EXTERNAL_MEMORY
 if((uintptr_t)&_ext_ram_noinit_end>end)end=(uintptr_t)&_ext_ram_noinit_end;
#endif
 end=(end+63u)&~(uintptr_t)63u;
 mapping=end-65536u;test_psram_low=mapping;test_psram_high=mapping+32u*1024u*1024u;
 expect(end,bytes-65536u);
 bytes=65536;expect(0,0);bytes=8u*1024u*1024u;
#else
 expect(mapping,bytes);assert(queries>128); /* Actual mapping, not HIGH-size. */
#endif
 initialized=false;expect(0,0);initialized=true;
 map_error=1;expect(0,0);map_error=0;
 gap=true;expect(0,0);gap=false;
 bytes=0;expect(0,0);bytes=8u*1024u*1024u;
 mapping=test_psram_high-65536;expect(0,0);
 mapping=test_psram_low-65536;expect(0,0);
 mapping=0;expect(0,0);
#endif
#endif
 puts("CoreS3 PSRAM: public init/size, actual contiguous MMU mapping, bounds/BSS/noinit and fail-closed configurations PASS");return 0;
}
