#pragma once
#include <m5os/types.h>
uint32_t crc32(const void *data, size_t len);     /* IEEE 802.3 (zlib-compatible) */
uint32_t crc32_update(uint32_t crc, const void *data, size_t len);   /* start with crc = 0 */
