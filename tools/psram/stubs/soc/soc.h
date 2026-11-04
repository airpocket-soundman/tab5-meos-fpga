/* Host fixture. */
#if M5OS_BOARD_CORES3
#include <stdint.h>
extern uintptr_t test_psram_low,test_psram_high;
#define SOC_EXTRAM_DATA_LOW test_psram_low
#define SOC_EXTRAM_DATA_HIGH test_psram_high
#endif
