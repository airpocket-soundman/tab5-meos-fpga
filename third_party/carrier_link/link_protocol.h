#pragma once
#include <stdbool.h>
#include <stdint.h>

// dock-gfx-v0: experimental extension, independent of the future DDR3 v1 ABI.
enum { LINK_ID=1, LINK_ECHO=2, LINK_INVERT=3, LINK_CAPS=4,
       GFX_INFO=0x10, GFX_COLOR=0x11, GFX_CLEAR=0x12, GFX_PIXEL=0x13,
       GFX_READ=0x14, GFX_ORIGIN=0x15, GFX_RECT=0x16,
       TRI_INFO=0x20, TRI_AX=0x21, TRI_AY=0x22, TRI_BX=0x23, TRI_BY=0x24,
       TRI_CX=0x25, TRI_CY=0x26, TRI_ZA=0x27, TRI_ZB=0x28, TRI_ZC=0x29,
       TRI_COLOR=0x2a, TRI_CLEAR=0x2b, TRI_DRAW=0x2c, TRI_READ=0x2d, TRI_DEPTH=0x2e,
       TRI_ROW=0x2f,
       FRACTAL_INFO=0x30, FRACTAL_X0=0x31, FRACTAL_Y0=0x32,
       FRACTAL_DX=0x33, FRACTAL_DY=0x34, FRACTAL_CX=0x35, FRACTAL_CY=0x36,
       FRACTAL_CONFIG=0x37, FRACTAL_DRAW=0x38,
       FRACTAL_CYCLES_LO=0x39, FRACTAL_CYCLES_HI=0x3a, TRI_TILE_READ=0x3b, TRI_TILE_CAPS=0x3c, TILE_READ=TRI_TILE_READ, TILE_CAPS=TRI_TILE_CAPS, CORE_CLOCK=0x3d, FRACTAL_CLOCK_MHZ=CORE_CLOCK, TRI_BATCH_CAPS=0x40, TRI_BATCH=0x41, LINK_TX_CAPTURE_CAPS=0x42, LINK_TX_BAD_WORD=0x43, LINK_TX_BAD_META=0x44 };
enum { TRI_ROW_WORDS=32, TRI_ROW_RESPONSE_BYTES=74, TRI_TILE_WORDS=1024, TRI_TILE_RESPONSE_BYTES=2058 };
static inline uint16_t link_crc16_extend(uint16_t c, const uint8_t *p, unsigned n) {
    // CRC-16/CCITT-FALSE, identical polynomial/seed/wire bytes. A 512-byte
    // table replaces eight bit steps per byte; incremental seeds are preserved.
    static const uint16_t table[256]={
        0x0000,0x1021,0x2042,0x3063,0x4084,0x50a5,0x60c6,0x70e7,
        0x8108,0x9129,0xa14a,0xb16b,0xc18c,0xd1ad,0xe1ce,0xf1ef,
        0x1231,0x0210,0x3273,0x2252,0x52b5,0x4294,0x72f7,0x62d6,
        0x9339,0x8318,0xb37b,0xa35a,0xd3bd,0xc39c,0xf3ff,0xe3de,
        0x2462,0x3443,0x0420,0x1401,0x64e6,0x74c7,0x44a4,0x5485,
        0xa56a,0xb54b,0x8528,0x9509,0xe5ee,0xf5cf,0xc5ac,0xd58d,
        0x3653,0x2672,0x1611,0x0630,0x76d7,0x66f6,0x5695,0x46b4,
        0xb75b,0xa77a,0x9719,0x8738,0xf7df,0xe7fe,0xd79d,0xc7bc,
        0x48c4,0x58e5,0x6886,0x78a7,0x0840,0x1861,0x2802,0x3823,
        0xc9cc,0xd9ed,0xe98e,0xf9af,0x8948,0x9969,0xa90a,0xb92b,
        0x5af5,0x4ad4,0x7ab7,0x6a96,0x1a71,0x0a50,0x3a33,0x2a12,
        0xdbfd,0xcbdc,0xfbbf,0xeb9e,0x9b79,0x8b58,0xbb3b,0xab1a,
        0x6ca6,0x7c87,0x4ce4,0x5cc5,0x2c22,0x3c03,0x0c60,0x1c41,
        0xedae,0xfd8f,0xcdec,0xddcd,0xad2a,0xbd0b,0x8d68,0x9d49,
        0x7e97,0x6eb6,0x5ed5,0x4ef4,0x3e13,0x2e32,0x1e51,0x0e70,
        0xff9f,0xefbe,0xdfdd,0xcffc,0xbf1b,0xaf3a,0x9f59,0x8f78,
        0x9188,0x81a9,0xb1ca,0xa1eb,0xd10c,0xc12d,0xf14e,0xe16f,
        0x1080,0x00a1,0x30c2,0x20e3,0x5004,0x4025,0x7046,0x6067,
        0x83b9,0x9398,0xa3fb,0xb3da,0xc33d,0xd31c,0xe37f,0xf35e,
        0x02b1,0x1290,0x22f3,0x32d2,0x4235,0x5214,0x6277,0x7256,
        0xb5ea,0xa5cb,0x95a8,0x8589,0xf56e,0xe54f,0xd52c,0xc50d,
        0x34e2,0x24c3,0x14a0,0x0481,0x7466,0x6447,0x5424,0x4405,
        0xa7db,0xb7fa,0x8799,0x97b8,0xe75f,0xf77e,0xc71d,0xd73c,
        0x26d3,0x36f2,0x0691,0x16b0,0x6657,0x7676,0x4615,0x5634,
        0xd94c,0xc96d,0xf90e,0xe92f,0x99c8,0x89e9,0xb98a,0xa9ab,
        0x5844,0x4865,0x7806,0x6827,0x18c0,0x08e1,0x3882,0x28a3,
        0xcb7d,0xdb5c,0xeb3f,0xfb1e,0x8bf9,0x9bd8,0xabbb,0xbb9a,
        0x4a75,0x5a54,0x6a37,0x7a16,0x0af1,0x1ad0,0x2ab3,0x3a92,
        0xfd2e,0xed0f,0xdd6c,0xcd4d,0xbdaa,0xad8b,0x9de8,0x8dc9,
        0x7c26,0x6c07,0x5c64,0x4c45,0x3ca2,0x2c83,0x1ce0,0x0cc1,
        0xef1f,0xff3e,0xcf5d,0xdf7c,0xaf9b,0xbfba,0x8fd9,0x9ff8,
        0x6e17,0x7e36,0x4e55,0x5e74,0x2e93,0x3eb2,0x0ed1,0x1ef0,
    };
    while(n--) c=(uint16_t)((c<<8)^table[(uint8_t)((c>>8)^*p++)]);
    return c;
}
static inline uint16_t link_crc16(const uint8_t *p, unsigned n) {
    return link_crc16_extend(0xffff,p,n);
}
static inline void link_make_request(uint8_t q[8], uint8_t op, uint8_t seq, uint8_t a, uint8_t b) {
    q[0]=0xa5; q[1]=op; q[2]=seq; q[3]=2; q[4]=a; q[5]=b;
    uint16_t c=link_crc16(q,6); q[6]=(uint8_t)c; q[7]=(uint8_t)(c>>8);
}
static inline bool link_validate_response(const uint8_t r[8], const uint8_t q[8]) {
    uint16_t c=link_crc16(r,6);
    if(r[0]!=0x5a || r[1]!=0 || r[2]!=q[2] || r[3]!=2 || r[6]!=(uint8_t)c || r[7]!=(uint8_t)(c>>8)) return false;
    switch(q[1]&0x7f) {
        case LINK_TX_CAPTURE_CAPS: return q[4]==0 && q[5]==0 && r[4]==3 && r[5]==1;
        case LINK_TX_BAD_WORD: return q[4]<4 && q[5]==0;
        case LINK_TX_BAD_META: return q[4]<3 && q[5]==0 && (q[4]!=1 || ((r[4]&~7)==0 && (r[4]&1) && r[5]==8));
        case TRI_BATCH_CAPS: return q[4]==0 && q[5]==0 && r[4]==64 && r[5]==1;
        case TRI_BATCH: return q[4]>=1 && q[4]<=64 && q[5]==1 && r[4]==q[4] && r[5]==1;
        case LINK_ID: return r[4]=='T' && r[5]=='P';
        case LINK_CAPS: return r[4]==8 && r[5]==1;
        case LINK_ECHO: return r[4]==q[4] && r[5]==q[5];
        case LINK_INVERT: return r[4]==(uint8_t)(q[4]^255) && r[5]==(uint8_t)(q[5]^255);
        case GFX_INFO: case TRI_INFO: return r[4]==32 && r[5]==32;
        case TRI_AX: case TRI_AY: case TRI_BX: case TRI_BY: case TRI_CX: case TRI_CY:
        case TRI_ZA: case TRI_ZB: case TRI_ZC: case TRI_COLOR:
            return r[4]==q[4] && r[5]==q[5];
        case FRACTAL_INFO: return ((r[4]==3 || r[4]==7 || r[4]==23) && r[5]==1) || ((r[4]==15 || r[4]==31) && r[5]==2);
        case FRACTAL_X0: case FRACTAL_Y0: case FRACTAL_DX: case FRACTAL_DY:
        case FRACTAL_CX: case FRACTAL_CY: case FRACTAL_CONFIG:
            return r[4]==q[4] && r[5]==q[5];
        case CORE_CLOCK: return q[4]==0 && q[5]==0 && r[4]>=1 && r[4]<=200 && r[5]==0;
        case FRACTAL_CYCLES_LO: case FRACTAL_CYCLES_HI: return q[4]==0 && q[5]==0;
        case FRACTAL_DRAW: return r[4]==0 && r[5]==4;
        case TRI_CLEAR: return r[4]==0 && r[5]==0;
        case TRI_DRAW: return ((uint16_t)r[4] | (uint16_t)r[5]<<8)<=1024;
        case TRI_READ: case TRI_DEPTH: return true; // Compare complete color/depth on host.
        case TRI_TILE_CAPS: return q[4]==0 && q[5]==0 && r[4]==3 && r[5]==1;
        case TRI_TILE_READ: return q[4]==0 && q[5]<2 && r[4]==0 && r[5]==q[5];
        case TRI_ROW: return q[4]<32 && q[5]<2 && r[4]==q[4] && r[5]==q[5];
        case GFX_COLOR: case GFX_PIXEL: case GFX_ORIGIN: case GFX_RECT:
            return r[4]==q[4] && r[5]==q[5];
        case GFX_CLEAR: return r[4]==0 && r[5]==0;
        case GFX_READ: return true; // Caller must compare against expected image.
        default: return false;
    }
}
// TRI_ROW success only: 8-byte header + 64-byte row + 2-byte bound CRC.
// A nonzero status is a normal eight-byte error, never a 74-byte success.
static inline bool link_validate_row_response(const uint8_t r[74], const uint8_t q[8]) {
    if((q[1]&0x7f)!=TRI_ROW || !link_validate_response(r,q)) return false;
    uint16_t c=link_crc16_extend(link_crc16(r,6),r+8,64);
    return r[72]==(uint8_t)c && r[73]==(uint8_t)(c>>8);
}

// TILE_READ success: header8 + row-major little-endian words2048 + boundCRC2.
static inline bool link_validate_tile_response(const uint8_t r[2058],const uint8_t q[8]) {
    if((q[1]&0x7f)!=TRI_TILE_READ || !link_validate_response(r,q)) return false;
    uint16_t c=link_crc16_extend(link_crc16(r,6),r+8,2048);
    return r[2056]==(uint8_t)c && r[2057]==(uint8_t)(c>>8);
}
