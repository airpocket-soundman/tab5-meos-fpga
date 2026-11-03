#pragma once
#include "fpga_mock.h"
#include "fpga_gfx.h"
typedef struct {
    uint16_t pixels[FPGA_GFX_PIXELS],color;
    unsigned x,y;
    bool initialized;
} fpga_gfx_mock_t;
void fpga_gfx_mock_init(fpga_gfx_mock_t *gfx,fpga_mock_t *transport);
