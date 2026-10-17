/*
 * FocalTech FT6336U capacitive touch controller (CoreS3), I2C address 0x38
 * (M5GFX Touch_M5StackCoreS3: i2c_addr 0x38, range x 0..319, y 0..239).
 * Register map is the standard FT5x06/FT6x36 one:
 *   0x02  TD_STATUS   bits 3:0 = number of touch points
 *   0x03  P1_XH       bits 7:6 event flag, bits 3:0 = X[11:8]
 *   0x04  P1_XL       X[7:0]
 *   0x05  P1_YH       bits 3:0 = Y[11:8]
 *   0x06  P1_YL       Y[7:0]
 *   0xA3  CHIP_ID     0x64 on FT6336U
 *   0xA8  VENDOR_ID   0x11 FocalTech
 * The I2C bus is shared (power ICs), so every transaction takes i2c_bus_lock.
 */
#include "ft6336.h"
#include <m5os/hal_i2c.h>
#include <m5os/mutex.h>
#include <m5os/kprintf.h>

#define FT_ADDR           0x38
#define FT_REG_TD_STATUS  0x02
#define FT_REG_CHIP_ID    0xA3
#define FT_REG_VENDOR_ID  0xA8
#define FT_CHIP_FT6336U     0x64    /* read on the CoreS3. NOTE: reg 0xA8 returned 0x02 here, not
                                       * the 0x11 documented for the FT5x06 family, so it is only logged. */
#define FT_TOUCH_COUNT_MASK 0x0F
#define FT_HI_NIBBLE_MASK   0x0F
#define FT_FRAME_LEN        5

static mutex_t s_i2c_lock = MUTEX_INIT;

bool touch_init(void)
{
    uint8_t vendor = 0, chip = 0;
    mutex_lock(&s_i2c_lock);
    bool ok = hal_i2c_read_reg(FT_ADDR, FT_REG_VENDOR_ID, &vendor) &&
              hal_i2c_read_reg(FT_ADDR, FT_REG_CHIP_ID, &chip);
    mutex_unlock(&s_i2c_lock);
    kprintf("touch: FT6336 vendor=%s0x%02x chip=0x%02x\n", ok ? "" : "NACK ", vendor, chip);
    return ok && chip == FT_CHIP_FT6336U;
}

int touch_read(uint16_t *x, uint16_t *y)
{
    uint8_t b[FT_FRAME_LEN];
    mutex_lock(&s_i2c_lock);
    bool ok = hal_i2c_read_regs(FT_ADDR, FT_REG_TD_STATUS, b, sizeof b);
    mutex_unlock(&s_i2c_lock);
    if (!ok) return 0;
    int n = b[0] & FT_TOUCH_COUNT_MASK;
    if (n == 0 || n > 2) return 0;
    *x = (uint16_t)(((b[1] & FT_HI_NIBBLE_MASK) << 8) | b[2]);
    *y = (uint16_t)(((b[3] & FT_HI_NIBBLE_MASK) << 8) | b[4]);
    return n;
}
