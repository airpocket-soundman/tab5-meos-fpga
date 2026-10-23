#pragma once
#include <m5os/io.h>
typedef struct {
    int sda, scl;
    uint32_t frequency_hz;       /* 1000..400000; integer-us bitbang <= requested */
    uint32_t stretch_timeout_us; /* 0:10000, maximum100000 */
    uint32_t transfer_timeout_us;/* 0:2000000, maximum10000000 */
} io_i2c_config_t;
int io_i2c_open(io_owner_t owner,const io_i2c_config_t *config,uint32_t *handle);
/* Seven-bit address. Both nonzero lengths issue a repeated START, no intervening STOP.
 * NULL buffers are accepted only for zero length. Maximum4096 bytes per direction.
 * Zero lengths probe the address in write direction. Returns0 or a negative error.
 * Single master only; external pull-ups and correct voltage are required. */
int io_i2c_transfer(io_owner_t owner,uint32_t handle,uint8_t address,
                    const uint8_t *tx,size_t tx_length,uint8_t *rx,size_t rx_length);
int io_i2c_cancel(io_owner_t owner,uint32_t handle);
int io_i2c_close(io_owner_t owner,uint32_t handle);
void io_i2c_release_owner(io_owner_t owner);
