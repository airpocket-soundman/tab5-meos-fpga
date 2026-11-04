#pragma once
#include <stdint.h>
#include <stdbool.h>

/* /dev/fpga: fixed graphics commands, APP_PERM_DISPLAY. No raw GPIO access.
 * One synchronous32x32 RGB565 tile. Output is committed only after complete
 * validated device readback and safe idle/close. Held-session callers must
 * require successful END before presenting any part of the frame. No CPU fallback. */
#define FPGA_FRACTAL_ABI_VERSION 1u
#define FPGA_IOC_FRACTAL_RENDER 0x0501u
/* Optional fixed-policy session, NULL argument. BEGIN negotiates once, then
 * RENDER may be repeated by the owning task. END releases all bus resources.
 * Owner close/task exit also tears down; communication errors close without retry.
 * Older firmware returns ERR_NOSYS for these optional operations. */
#define FPGA_IOC_FRACTAL_BEGIN  0x0502u
#define FPGA_IOC_FRACTAL_END    0x0503u
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
