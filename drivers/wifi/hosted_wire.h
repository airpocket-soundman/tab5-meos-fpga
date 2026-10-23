/* Wire-format recognition only; no ESP-Hosted implementation is imported. */
#pragma once
#include <m5os/types.h>

static inline uint16_t hosted_wire_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/* ESP-Hosted-NG uses a different 12-byte header and Linux-oriented control plane.
 * Recognize a complete INTERNAL boot event rather than guessing from interface 3.
 * https://github.com/espressif/esp-hosted/tree/legacy/fg-ng/esp_hosted_ng
 */
static inline bool hosted_wire_is_ng_boot(const uint8_t *frame, size_t size)
{
    if (!frame || size < 20 || frame[0] != 3 || frame[1] || frame[2] || frame[3])
        return false;
    uint16_t len = hosted_wire_u16(frame + 4);
    uint16_t off = hosted_wire_u16(frame + 6);
    if (off < 12 || off > 16 || len < 8 || off > size || len > size - off)
        return false;
    const uint8_t *event = frame + off;
    /* NG event_header.len counts data TLVs plus its one-byte event.len;
     * the outer transport length also includes the eight-byte event/padding. */
    if (event[0] != 1 || event[1] != 0 || hosted_wire_u16(event + 2) != len - 7 ||
        event[4] != len - 8) return false;
    bool chip = false, capability = false;
    size_t pos = 8;
    while (pos < len) {
        if (len - pos < 2) return false;
        uint8_t tag = event[pos++], n = event[pos++];
        if (!n || n > len - pos) return false;
        if (tag == 3) { /* firmware chip ID: ESP32-C6 */
            if (chip || n != 1 || event[pos] != 0x0d) return false;
            chip = true;
        } else if (tag == 0) { /* capability: SDIO WLAN */
            if (capability || n != 1 || !(event[pos] & 1)) return false;
            capability = true;
        }
        pos += n;
    }
    return chip && capability;
}
