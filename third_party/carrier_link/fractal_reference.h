#pragma once
#include <stdint.h>
#include <stdbool.h>
// Q4.12 viewport, signed positions/Julia constants; positive unsigned steps.
// The recurrence uses floor division by 4096 (arithmetic right shift in RTL).
typedef struct {
    int16_t x0, y0;
    uint16_t dx, dy;
    int16_t cx, cy;
    uint8_t limit, julia; // mode bits: bit0 Julia, bit1 vivid palette; ABI unchanged.
} fractal_config;
static inline int32_t fractal_floor12(int64_t value) {
    return (int32_t)(value>=0 ? value/4096 : -((-value+4095)/4096));
}
static inline uint16_t fractal_palette(unsigned iterations, unsigned limit) {
    if(iterations==limit) return 0x0042;
    unsigned n=iterations*3, r=32+(n*9%224), g=60+(n*5%196), b=100+(n*3%156);
    return (uint16_t)(((r&248)<<8)|((g&252)<<3)|(b>>3));
}
static inline uint16_t fractal_vivid_palette(unsigned iterations,unsigned limit) {
    static const uint16_t colors[32]={
        0x000a, 0x0010, 0x0018, 0x011f, 0x031f, 0x051f, 0x071f, 0x07ff, 0x47fc, 0x87f4, 0xc7ec, 0xf7e4, 0xffe0, 0xff00, 0xfe00, 0xfd00, 0xfc00, 0xfb00, 0xfa00, 0xf880, 0xf800, 0xf808, 0xf810, 0xf818, 0xf81f, 0xd01f, 0xa01f, 0x701f, 0x501c, 0x3016, 0x1810, 0x080c
    };
    return iterations==limit ? 0x0000 : colors[iterations&31];
}
static inline uint16_t fractal_palette_mode(unsigned iterations,unsigned limit,unsigned mode) {
    return (mode&2) ? fractal_vivid_palette(iterations,limit) : fractal_palette(iterations,limit);
}
static inline bool fractal_config_valid(const fractal_config *config) {
    return config && config->limit>=1 && config->limit<=128 && config->julia<=3 &&
        config->dx>=1 && config->dx<=32767 && config->dy>=1 && config->dy<=32767 &&
        (int32_t)config->x0+31*(int32_t)config->dx<=32767 &&
        (int32_t)config->y0+31*(int32_t)config->dy<=32767;
}
// Caller supplies validated configuration and a tile-local coordinate in 0..31.
static inline uint16_t fractal_reference_pixel(const fractal_config *config,unsigned x,unsigned y,unsigned *work) {
    const int32_t px=(int32_t)config->x0+(int32_t)x*config->dx;
    const int32_t py=(int32_t)config->y0+(int32_t)y*config->dy;
    int32_t zx=(config->julia&1)?px:0, zy=(config->julia&1)?py:0;
    const int32_t cx=(config->julia&1)?config->cx:px, cy=(config->julia&1)?config->cy:py;
    unsigned iterations=0;
    while((int64_t)zx*zx+(int64_t)zy*zy<67108864 && iterations<config->limit) {
        const int32_t next_x=fractal_floor12((int64_t)zx*zx-(int64_t)zy*zy)+cx;
        zy=fractal_floor12(2*(int64_t)zx*zy)+cy; zx=next_x; ++iterations;
    }
    if(work) *work=iterations;
    return fractal_palette_mode(iterations,config->limit,config->julia);
}
static inline bool fractal_reference_tile(const fractal_config *config,uint16_t pixels[1024],uint32_t *work) {
    if(!pixels || !fractal_config_valid(config)) return false;
    uint32_t count=0;
    for(unsigned y=0;y<32;y++) for(unsigned x=0;x<32;x++) {
        unsigned iterations; pixels[y*32+x]=fractal_reference_pixel(config,x,y,&iterations); count+=iterations;
    }
    if(work) *work=count;
    return true;
}
