#pragma once
#include <m5os/fpga_link.h>
#include <m5os/io.h>
#ifndef M5OS_FPGA_DOCK_VERIFIED
#define M5OS_FPGA_DOCK_VERIFIED 0
#endif
typedef struct { io_owner_t owner; bool claimed, armed, cancel; } fpga_gpio_t;
/* explicit_arm alone never overrides the unverified board-profile gate. */
int fpga_gpio_open(fpga_gpio_t *bus, bool explicit_arm);
void fpga_gpio_close(fpga_gpio_t *bus);
extern const fpga_link_ops_t fpga_gpio_ops;
