#pragma once
#include <m5os/types.h>

/* External RAM as mapped by the platform start-up code.  size == 0 if absent. */
void hal_psram_region(uintptr_t *base, size_t *size);

/* Tab5 only: the slice at the top of PSRAM that hal_psram_region() leaves out (M5OS_PSRAM_RESERVE bytes). */
void hal_psram_reserve_region(uintptr_t *base, size_t *size);
