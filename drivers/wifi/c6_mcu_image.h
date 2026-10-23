#pragma once
#include <m5os/types.h>
#include <m5os/sha256.h>

/* Exact M5Stack Tab5 MCU application extracted by prepare_c6_mcu_image.py. */
static inline bool c6_mcu_image_valid(const uint8_t *data, size_t size)
{
    static const uint8_t expected[32] = {
        0xcb,0x16,0x9b,0x09,0xa8,0x90,0x03,0x7a,
        0xf4,0x25,0x0b,0xd9,0x3c,0x33,0xc3,0x94,
        0x03,0x7a,0x55,0xb8,0x01,0xba,0x1c,0xc0,
        0xa4,0xc1,0x21,0x0e,0x60,0xc6,0x89,0x02
    };
    if (!data || size != 1078896u || data[0] != 0xe9 || data[12] != 13 ||
        data[13] || data[23] != 1 || !data[1] || data[1] > 16) return false;
    size_t at = 24;
    uint8_t checksum = 0xef;
    for (unsigned segment = 0; segment < data[1]; segment++) {
        if (at > size - 8) return false;
        uint32_t n = (uint32_t)data[at+4] | ((uint32_t)data[at+5] << 8) |
                     ((uint32_t)data[at+6] << 16) | ((uint32_t)data[at+7] << 24);
        at += 8;
        if (n > size - at) return false;
        for (uint32_t i = 0; i < n; i++) checksum ^= data[at + i];
        at += n;
    }
    size_t checksum_at = ((at + 16) & ~(size_t)15) - 1;
    if (checksum_at >= size || data[checksum_at] != checksum ||
        checksum_at + 33 != size) return false;
    uint8_t digest[32];
    sha256(data, size - 32, digest);
    for (unsigned i = 0; i < 32; i++) if (digest[i] != data[size - 32 + i]) return false;
    sha256(data, size, digest);
    for (unsigned i = 0; i < 32; i++) if (digest[i] != expected[i]) return false;
    return true;
}
