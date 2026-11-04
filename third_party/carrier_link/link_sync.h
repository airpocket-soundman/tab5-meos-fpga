#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "link_protocol.h"

// Optional LINK transport capability, discovered by untagged LINK_CAPS.
// Request bit7 is part of the request CRC. Only tagged replies are prefixed.
enum { LINK_SYNC_TAG=0x80, LINK_SYNC_PREFIX=8, LINK_SYNC_BYTE=0xc3 };

// Find exactly one complete CRC-validated frame within an entire DMA capture.
// The capture includes 8 or16 margin bytes; all body bytes must
// exist contiguously in raw. Nothing is reconstructed or rendered on the CPU.
// Accept body+8 or body+16 captured bytes (body8/74/2058). Other sizes, invalid request CRC,
// zero/multiple valid candidate bodies or wrong body length are rejected.
// body and offset remain unchanged on failure. body may overlap raw.
static inline bool link_sync_unwrap(const uint8_t *raw,unsigned raw_len,
                                   const uint8_t q[8],uint8_t *body,
                                   unsigned body_len,unsigned *offset) {
    if(!raw || !q || !body || !(q[1]&LINK_SYNC_TAG) || q[0]!=0xa5 || q[3]!=2)
        return false;
    const uint16_t request_crc=link_crc16(q,6);
    if(q[6]!=(uint8_t)request_crc || q[7]!=(uint8_t)(request_crc>>8)) return false;
    const bool row=(q[1]&0x7f)==TRI_ROW;
    const bool tile=(q[1]&0x7f)==TRI_TILE_READ;
    const unsigned expected=tile?TRI_TILE_RESPONSE_BYTES:(row?TRI_ROW_RESPONSE_BYTES:8);
    if(body_len!=expected || (raw_len!=expected+8 && raw_len!=expected+16)) return false;
    const unsigned margin=raw_len-expected;
    unsigned matches=0,found=0;
    for(unsigned i=0;i<=margin;i++) {
        const bool valid=tile?link_validate_tile_response(raw+i,q):
                         (row?link_validate_row_response(raw+i,q):link_validate_response(raw+i,q));
        if(valid) { found=i; if(++matches>1) return false; }
    }
    if(matches!=1) return false;
    memmove(body,raw+found,expected);
    if(offset) *offset=found;
    return true;
}
