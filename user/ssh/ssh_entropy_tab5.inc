/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 * Adapted from ESP-IDF v5.4.4 bootloader_random_esp32p4.c for MeOS.
 * Uses ROM regi2c access under MeOS IRQ protection instead of FreeRTOS locks.
 */
#include "esp_random.h"
#include <m5os/arch.h>
#include <m5os/hal_time.h>
#include "soc/soc.h"
#include "soc/adc_reg.h"
#include "soc/pmu_reg.h"
#include "soc/regi2c_saradc.h"
#include "soc/hp_sys_clkrst_reg.h"
#include "soc/lp_adc_reg.h"

#include "esp_rom_regi2c.h"
#include "soc/lpperi_reg.h"

// TODO IDF-6497: once ADC API is supported, use the API instead of defining functions and constants here

#define I2C_SAR_ADC_INIT_CODE_VAL 2166
#define SSHD_REGI2C_WRITE(block, reg, value) \
    esp_rom_regi2c_write_mask(block, block##_HOSTID, reg, reg##_MSB, reg##_LSB, value)

typedef struct {
    int atten;
    int channel;
} pattern_item;

typedef struct {
    pattern_item item[4];
} pattern_table;

static void adc1_fix_initcode_set(uint32_t initcode_value)
{
    uint32_t msb = initcode_value >> 8;
    uint32_t lsb = initcode_value & 0xff;
    SSHD_REGI2C_WRITE(I2C_SAR_ADC, ADC_SAR1_INITIAL_CODE_HIGH_ADDR, msb);
    SSHD_REGI2C_WRITE(I2C_SAR_ADC, ADC_SAR1_INITIAL_CODE_LOW_ADDR, lsb);
}

//total 4 tables
static void hpadc_sar1_pattern_table_cfg(unsigned int table_idx, pattern_table table)
{
    uint32_t wdata = 0;
    wdata = (table.item[0].channel << 20 | table.item[0].atten << 18 |
            table.item[1].channel << 14|table.item[1].atten << 12 |
            table.item[2].channel << 8 |table.item[2].atten << 6 |
            table.item[3].channel << 2 |table.item[3].atten);
    WRITE_PERI_REG(ADC_SAR1_PATT_TAB1_REG + table_idx * 4, wdata);
}

static bool ssh_entropy_enable(void)
{
    pattern_table sar1_table[4] = {};
    uint32_t pattern_len = 0;

    SET_PERI_REG_MASK(LPPERI_CLK_EN_REG, LPPERI_CK_EN_LP_ADC);
    SET_PERI_REG_MASK(HP_SYS_CLKRST_SOC_CLK_CTRL2_REG, HP_SYS_CLKRST_REG_ADC_APB_CLK_EN);
    SET_PERI_REG_MASK(HP_SYS_CLKRST_PERI_CLK_CTRL23_REG, HP_SYS_CLKRST_REG_ADC_CLK_EN);

    SET_PERI_REG_MASK(RTCADC_MEAS1_MUX_REG, RTCADC_SAR1_DIG_FORCE);
    SET_PERI_REG_MASK(PMU_RF_PWC_REG,PMU_XPD_PERIF_I2C);

    uint32_t sar1_clk_div_num = GET_PERI_REG_BITS2((HP_SYS_CLKRST_PERI_CLK_CTRL24_REG),
            (HP_SYS_CLKRST_REG_ADC_SAR1_CLK_DIV_NUM_M),
            (HP_SYS_CLKRST_REG_ADC_SAR1_CLK_DIV_NUM_S));

    SET_PERI_REG_MASK(ADC_CTRL_REG_REG, ADC_START_FORCE); //start force 1

    adc1_fix_initcode_set(I2C_SAR_ADC_INIT_CODE_VAL);

    //  cfg pattern table
    sar1_table[0].item[0].channel = 10; //rand() % 6;
    sar1_table[0].item[0].atten = 3;
    sar1_table[0].item[1].channel = 10;
    sar1_table[0].item[1].atten = 3;
    sar1_table[0].item[2].channel = 10;
    sar1_table[0].item[2].atten = 3;
    sar1_table[0].item[3].channel = 10;
    sar1_table[0].item[3].atten = 3;

    hpadc_sar1_pattern_table_cfg(0, sar1_table[0]);
    SET_PERI_REG_BITS(ADC_CTRL_REG_REG, ADC_SAR1_PATT_LEN, pattern_len, ADC_SAR1_PATT_LEN_S);

    SET_PERI_REG_BITS(ADC_CTRL_REG_REG, ADC_XPD_SAR1_FORCE, 3, ADC_XPD_SAR1_FORCE_S);
    SET_PERI_REG_BITS(ADC_CTRL_REG_REG, ADC_XPD_SAR2_FORCE, 3, ADC_XPD_SAR2_FORCE_S);

    SSHD_REGI2C_WRITE(I2C_SAR_ADC, I2C_SAR_ADC_ENT_VDD_GRP1, 1);
    SSHD_REGI2C_WRITE(I2C_SAR_ADC, I2C_SAR_ADC_DTEST_VDD_GRP1, 0);

    CLEAR_PERI_REG_MASK(ADC_CTRL_REG_REG, ADC_START_FORCE);
    SET_PERI_REG_MASK(ADC_CTRL2_REG, ADC_TIMER_EN);
    SET_PERI_REG_BITS(ADC_CTRL2_REG, ADC_TIMER_TARGET, sar1_clk_div_num * 25, ADC_TIMER_TARGET_S);

    uint64_t deadline = hal_time_now_us() + 20000;
    while (GET_PERI_REG_MASK(ADC_INT_RAW_REG, ADC_SAR1_DONE_INT_RAW) == 0) {
        if (hal_time_now_us() >= deadline) return false;
    }

    SET_PERI_REG_MASK(ADC_INT_CLR_REG, ADC_APB_SARADC1_DONE_INT_CLR);
    return true;
}

static void ssh_entropy_disable(void)
{
    SSHD_REGI2C_WRITE(I2C_SAR_ADC, ADC_SAR1_INITIAL_CODE_LOW_ADDR, 0);
    SSHD_REGI2C_WRITE(I2C_SAR_ADC, ADC_SAR1_INITIAL_CODE_HIGH_ADDR, 0);
    SSHD_REGI2C_WRITE(I2C_SAR_ADC, I2C_SAR_ADC_ENT_VDD_GRP1, 0);
    SSHD_REGI2C_WRITE(I2C_SAR_ADC, I2C_SAR_ADC_DTEST_VDD_GRP1, 0);
}

/* MeOS adaptation: no FreeRTOS regi2c lock; the caller holds the RNG mutex.
 * ADC registers are restored after acquiring the startup seed. No ADC user may
 * run concurrently with this one-time operation (currently none on Tab5).
 */
static bool ssh_entropy_seed(uint8_t seed[32])
{
    const uint32_t regs[] = {
        LPPERI_CLK_EN_REG, HP_SYS_CLKRST_SOC_CLK_CTRL2_REG,
        HP_SYS_CLKRST_PERI_CLK_CTRL23_REG, RTCADC_MEAS1_MUX_REG,
        PMU_RF_PWC_REG, ADC_CTRL_REG_REG, ADC_CTRL2_REG, ADC_SAR1_PATT_TAB1_REG
    };
    uint32_t saved[sizeof(regs)/sizeof(regs[0])];
    uint32_t irq = arch_irq_disable();
    for (size_t i=0;i<sizeof(regs)/sizeof(regs[0]);i++) saved[i]=REG_READ(regs[i]);
    uint8_t initial_lo = esp_rom_regi2c_read_mask(I2C_SAR_ADC, I2C_SAR_ADC_HOSTID,
        ADC_SAR1_INITIAL_CODE_LOW_ADDR, ADC_SAR1_INITIAL_CODE_LOW_ADDR_MSB, ADC_SAR1_INITIAL_CODE_LOW_ADDR_LSB);
    uint8_t initial_hi = esp_rom_regi2c_read_mask(I2C_SAR_ADC, I2C_SAR_ADC_HOSTID,
        ADC_SAR1_INITIAL_CODE_HIGH_ADDR, ADC_SAR1_INITIAL_CODE_HIGH_ADDR_MSB, ADC_SAR1_INITIAL_CODE_HIGH_ADDR_LSB);
    uint8_t entropy = esp_rom_regi2c_read_mask(I2C_SAR_ADC, I2C_SAR_ADC_HOSTID,
        I2C_SAR_ADC_ENT_VDD_GRP1, I2C_SAR_ADC_ENT_VDD_GRP1_MSB, I2C_SAR_ADC_ENT_VDD_GRP1_LSB);
    uint8_t dtest = esp_rom_regi2c_read_mask(I2C_SAR_ADC, I2C_SAR_ADC_HOSTID,
        I2C_SAR_ADC_DTEST_VDD_GRP1, I2C_SAR_ADC_DTEST_VDD_GRP1_MSB, I2C_SAR_ADC_DTEST_VDD_GRP1_LSB);
    bool ok = ssh_entropy_enable();
    if (ok) esp_fill_random(seed,32);
    ssh_entropy_disable();
    SSHD_REGI2C_WRITE(I2C_SAR_ADC, ADC_SAR1_INITIAL_CODE_LOW_ADDR, initial_lo);
    SSHD_REGI2C_WRITE(I2C_SAR_ADC, ADC_SAR1_INITIAL_CODE_HIGH_ADDR, initial_hi);
    SSHD_REGI2C_WRITE(I2C_SAR_ADC, I2C_SAR_ADC_ENT_VDD_GRP1, entropy);
    SSHD_REGI2C_WRITE(I2C_SAR_ADC, I2C_SAR_ADC_DTEST_VDD_GRP1, dtest);
    for (size_t i=sizeof(regs)/sizeof(regs[0]);i>0;i--) REG_WRITE(regs[i-1],saved[i-1]);
    arch_irq_restore(irq);
    return ok;
}
#undef SSHD_REGI2C_WRITE
