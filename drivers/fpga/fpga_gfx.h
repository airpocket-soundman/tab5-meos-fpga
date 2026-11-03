#pragma once
#include <m5os/fpga_link.h>
#define FPGA_GFX_WIDTH 32u
#define FPGA_GFX_HEIGHT 32u
#define FPGA_GFX_PIXELS 1024u
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
