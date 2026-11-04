#pragma once
#include <stdint.h>
/* Dock D0..7 = GPIO18,19,5,3,4,2,48,47. Two actual register samples. */
static inline uint8_t parlio_gpio_byte(uint32_t low,uint32_t high) {
    return (uint8_t)(((low>>18)&3u)|((low>>3)&4u)|(low&24u)|
                     ((low<<3)&32u)|((high>>10)&64u)|((high>>8)&128u));
}
