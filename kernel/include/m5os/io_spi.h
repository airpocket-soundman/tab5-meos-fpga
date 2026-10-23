#pragma once
#include <m5os/io.h>
typedef struct {
    int sck, mosi, miso, cs;
    uint8_t mode;               /* 0..3, eight-bit words, MSB first */
    uint32_t frequency_hz;      /* 1000..500000; integer-us bitbang <= requested */
    uint32_t transfer_timeout_us;/* 0:2000000, maximum10000000 */
} io_spi_config_t;
int io_spi_open(io_owner_t owner,const io_spi_config_t *config,uint32_t *handle);
/* One active-low CS assertion per call, 8-bit full duplex. NULL tx sends0xff;
 * NULL rx discards input. Maximum4096 bytes. Returns0 or a negative error.
 * No DMA or hardware peripheral acceleration; timing can be stretched by tasks. */
int io_spi_transfer(io_owner_t owner,uint32_t handle,const uint8_t *tx,uint8_t *rx,size_t length);
int io_spi_cancel(io_owner_t owner,uint32_t handle);
int io_spi_close(io_owner_t owner,uint32_t handle);
void io_spi_release_owner(io_owner_t owner);
