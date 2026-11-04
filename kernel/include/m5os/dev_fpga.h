#pragma once
#include <stdint.h>
#include <stdbool.h>

/* /dev/fpga: fixed graphics commands, APP_PERM_DISPLAY. No raw GPIO access.
 * One synchronous32x32 RGB565 tile. Output is committed only after complete
 * validated device readback and safe physical-session close. No CPU fallback. */
#define FPGA_FRACTAL_ABI_VERSION 1u
#define FPGA_IOC_FRACTAL_RENDER 0x0501u
#define FPGA_FRACTAL_PIXELS 1024u
typedef struct {
    uint32_t version,size;
    int16_t x0,y0,cx,cy; /* signed Q4.12 */
    uint16_t dx,dy;     /* unsigned Q4.12 positive steps */
    uint8_t iterations,mode; /*1..128; bit0 Julia,bit1 vivid palette */
    uint16_t *pixels;
    uint32_t pixel_count;
    uint32_t elapsed_us,engine_cycles;
    bool cycles_valid;
} fpga_fractal_request_t;
