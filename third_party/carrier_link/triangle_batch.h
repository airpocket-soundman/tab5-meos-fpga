#pragma once
#include "link_protocol.h"
// Caller owns output; returns0 on invalid args/capacity. Records contain the
// existing ten16-bit setter values, flattened in triangle input order.
static inline unsigned triangle_batch_encode(uint8_t *out,unsigned capacity,
    uint8_t seq,bool tagged,const uint16_t *records,unsigned count) {
    if(!out || !records || count<1 || count>64 || capacity<10+20*count) return 0;
    link_make_request(out,(uint8_t)(TRI_BATCH|(tagged?128:0)),seq,(uint8_t)count,1);
    for(unsigned i=0;i<count*10;i++) {out[8+2*i]=(uint8_t)records[i];out[9+2*i]=(uint8_t)(records[i]>>8);}
    uint16_t c=link_crc16_extend(link_crc16(out,6),out+8,20*count);
    out[8+20*count]=(uint8_t)c;out[9+20*count]=(uint8_t)(c>>8);
    return 10+20*count;
}
