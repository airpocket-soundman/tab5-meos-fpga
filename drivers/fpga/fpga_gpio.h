#pragma once
#include <m5os/fpga_link.h>
#include <m5os/io.h>

/* One caller-owned session. Claim pins once for an entire diagnostic sequence.
 * No activation at boot; caller must have checked the Dock wiring/power/SW0.
 * After failure reset the FPGA with SW0 or Key_0 before opening again. */
typedef struct {
    io_owner_t owner;
    int error;
    bool claimed, failed;
    bool (*cancel)(void *);
    void *cancel_context;
} fpga_gpio_t;
int fpga_gpio_open(fpga_gpio_t *gpio, const char *profile);
void fpga_gpio_close(fpga_gpio_t *gpio);
extern const fpga_link_ops_t fpga_gpio_ops;
