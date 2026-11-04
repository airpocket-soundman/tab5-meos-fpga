#pragma once
#include <m5os/fpga_link.h>
#define FPGA_GFX_WIDTH 32u
#define FPGA_GFX_HEIGHT 32u
#define FPGA_GFX_PIXELS 1024u
#define FPGA_GFX_VERIFY_SCENES 22u
#define FPGA_GFX_VERIFY_PIXELS (FPGA_GFX_VERIFY_SCENES * FPGA_GFX_PIXELS)
/* Caller must serialize the whole command sequence/session, including readback.
 * Dock IO ownership supplies this exclusion in the shell. No retries. */
int fpga_gfx_info(fpga_link_t *link);
int fpga_gfx_color(fpga_link_t *link,uint16_t color);
int fpga_gfx_clear(fpga_link_t *link,uint16_t color);
int fpga_gfx_pixel(fpga_link_t *link,unsigned x,unsigned y,uint16_t color);
int fpga_gfx_rect(fpga_link_t *link,unsigned x,unsigned y,unsigned w,unsigned h,uint16_t color);
int fpga_gfx_read(fpga_link_t *link,unsigned x,unsigned y,uint16_t *color);
/* ID/INFO, asymmetric drawing, all-pixel readback/compare. Buffer contains
 * received pixels, never a host-generated substitute. No display on failure. */
int fpga_gfx_diagnostic(fpga_link_t *link,uint16_t pixels[FPGA_GFX_PIXELS],unsigned *compared);
/* Fixed correctness suite: asymmetric scene, black/white/primaries, then all
 * sixteen RGB565 bit planes. Reuses one readback buffer; never presents to LCD.
 * Counters describe only pixels/scenes actually checked before the first error.
 * Uses existing v0 commands and the caller's transport/cancellation callbacks. */
int fpga_gfx_verify(fpga_link_t *link,uint16_t pixels[FPGA_GFX_PIXELS],
                    unsigned *compared,unsigned *scenes);
