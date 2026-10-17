/*
 * CoreS3 power / reset bring-up over the internal I2C bus.
 *
 * Every address and value below is taken from M5Stack's own drivers (MIT):
 *   M5GFX     src/M5GFX.cpp          CoreS3 autodetect block, Light_M5StackCoreS3
 *   M5Unified src/utility/Power_Class.inl   CoreS3 PMIC setup
 * and is only changed where noted.  Nothing is guessed.
 *
 *   AW9523 (I/O expander, 0x58): P1_1 = LCD_RST, P1_7 = BOOST_EN, ...
 *   AXP2101 (PMIC, 0x34): DLDO1 = LCD backlight, ALDOx = peripheral rails.
 */
#include <m5os/board.h>
#include <m5os/hal_i2c.h>
#include <m5os/hal_lcd_bus.h>
#include <m5os/kprintf.h>

#define I2C_ADDR_AXP2101      0x34
#define I2C_ADDR_AW9523       0x58

#define AW9523_REG_ID         0x10     /* reads 0x23 on a real AW9523 */
#define AW9523_ID_VALUE       0x23
#define AW9523_REG_OUT_P0     0x02
#define AW9523_REG_OUT_P1     0x03
#define AW9523_REG_CFG_P0     0x04     /* 1 = input */
#define AW9523_REG_CFG_P1     0x05
#define AW9523_REG_GCR        0x11
#define AW9523_REG_LEDMODE_P0 0x12
#define AW9523_REG_LEDMODE_P1 0x13

#define AXP_REG_CHIP_ID       0x03
#define AXP_REG_UVP           0x23
#define AXP_REG_LDO_ENABLE0   0x90     /* bit7 = DLDO1 (backlight) */
#define AXP_LDO_ENABLE0_ALL_BUT_DLDO1  0x3F   /* M5's 0xBF with bit7 (backlight) held off until the screen is cleared */
#define AXP_BIT_DLDO1         0x80
#define AXP_REG_ALDO1         0x92
#define AXP_REG_ALDO2         0x93
#define AXP_REG_ALDO3         0x94
#define AXP_REG_ALDO4         0x95
#define AXP_REG_DLDO1_VOLT    0x99     /* 0.5 V + 0.1 V * value */
#define AXP_ALDO_V(tenths)    ((tenths) - 5)   /* M5: "33 - 5" = 3.3 V */
#define AXP_UVP_POWEROFF_MASK 0x1F     /* DCDC1-5 UVP power-off disable (M5: bitOff 0x1F) */
#define DLDO1_VOLT_MAX        28       /* 3.3 V; M5 maps brightness 255 -> 28 */

bool board_init(void)
{
    hal_i2c_init();

    uint8_t aw_id = 0, axp_id = 0;
    bool aw_ok  = hal_i2c_read_reg(I2C_ADDR_AW9523, AW9523_REG_ID, &aw_id);
    bool axp_ok = hal_i2c_read_reg(I2C_ADDR_AXP2101, AXP_REG_CHIP_ID, &axp_id);
    kprintf("board: AW9523 id=%s0x%02x (expect 0x%02x)  AXP2101 chip=%s0x%02x\n",
            aw_ok ? "" : "NACK ", aw_id, AW9523_ID_VALUE, axp_ok ? "" : "NACK ", axp_id);
    if (!aw_ok || !axp_ok || aw_id != AW9523_ID_VALUE) {
        return false;
    }

    /* AW9523: outputs first, then direction (same order as M5GFX).  Output
     * P1 bit1 = 1 releases LCD_RST; P0 bits 0,2 / P1 bit0 are the board's
     * other enables.  The VBUS-boost bits (P1_7) are deliberately not set. */
    hal_i2c_bit_on (I2C_ADDR_AW9523, AW9523_REG_OUT_P0, 0x05);
    hal_i2c_bit_on (I2C_ADDR_AW9523, AW9523_REG_OUT_P1, 0x03);
    hal_i2c_write_reg(I2C_ADDR_AW9523, AW9523_REG_CFG_P0, 0x18);
    hal_i2c_write_reg(I2C_ADDR_AW9523, AW9523_REG_CFG_P1, 0x0C);
    hal_i2c_write_reg(I2C_ADDR_AW9523, AW9523_REG_GCR, 0x10);          /* P0 push-pull */
    hal_i2c_write_reg(I2C_ADDR_AW9523, AW9523_REG_LEDMODE_P0, 0xFF);   /* all pins GPIO mode */
    hal_i2c_write_reg(I2C_ADDR_AW9523, AW9523_REG_LEDMODE_P1, 0xFF);

    /* AXP2101 */
    hal_i2c_bit_off  (I2C_ADDR_AXP2101, AXP_REG_UVP, AXP_UVP_POWEROFF_MASK);
    hal_i2c_write_reg(I2C_ADDR_AXP2101, AXP_REG_DLDO1_VOLT, 0);
    hal_i2c_write_reg(I2C_ADDR_AXP2101, AXP_REG_LDO_ENABLE0, AXP_LDO_ENABLE0_ALL_BUT_DLDO1);
    hal_i2c_write_reg(I2C_ADDR_AXP2101, AXP_REG_ALDO1, AXP_ALDO_V(18));
    hal_i2c_write_reg(I2C_ADDR_AXP2101, AXP_REG_ALDO2, AXP_ALDO_V(33));
    hal_i2c_write_reg(I2C_ADDR_AXP2101, AXP_REG_ALDO3, AXP_ALDO_V(33));
    hal_i2c_write_reg(I2C_ADDR_AXP2101, AXP_REG_ALDO4, AXP_ALDO_V(33));

    hal_lcd_bus_init();
    hal_delay_ms(10);
    return true;
}

void board_backlight(uint8_t level)
{
    if (level == 0) {
        hal_i2c_bit_off(I2C_ADDR_AXP2101, AXP_REG_LDO_ENABLE0, AXP_BIT_DLDO1);
        hal_i2c_write_reg(I2C_ADDR_AXP2101, AXP_REG_DLDO1_VOLT, 0);
        return;
    }
    /* M5GFX: brightness -> (brightness + 641) >> 5, i.e. 255 -> 28 (3.3 V) */
    uint8_t v = (uint8_t)(((uint32_t)level + 641u) >> 5);
    if (v > DLDO1_VOLT_MAX) v = DLDO1_VOLT_MAX;
    hal_i2c_write_reg(I2C_ADDR_AXP2101, AXP_REG_DLDO1_VOLT, v);
    hal_i2c_bit_on(I2C_ADDR_AXP2101, AXP_REG_LDO_ENABLE0, AXP_BIT_DLDO1);
}
