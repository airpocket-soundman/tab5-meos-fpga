#include <assert.h>
#include <m5os/errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* Register-model fault tests: actual production native conversion function is inserted below. */
typedef union {
    uint32_t val, ck_en_lp_adc, meas1_start_sar, meas2_start_sar;
} reg_t;
static struct {
    reg_t soc_clk_ctrl2, peri_clk_ctrl23;
} HP_SYS_CLKRST;
static struct {
    reg_t clk_en;
} LPPERI;
static struct {
    reg_t ctrl_reg;
} ADC;
static struct {
    reg_t force_wpd_sar, reader1_ctrl, reader2_ctrl, meas1_mux, meas2_mux, meas1_ctrl2, meas2_ctrl2,
        atten1, atten2;
} LP_ADC;
static uint32_t pmu;
static uint8_t analog[6];
static int GPIO;
static uint32_t raw_result;
static bool done;
static uint64_t ticks;
static unsigned pad_calls, starts;
#define PMU_RF_PWC_REG 0
#define PMU_XPD_PERIF_I2C 8
#define REG_READ(r) pmu
#define REG_WRITE(r, v) (pmu = (v))
#define REG_SET_BIT(r, v) (pmu |= (v))
#define PIN_FUNC_GPIO 0
#define ADC_SAR1_INITIAL_CODE_LOW_ADDR 0
#define ADC_SAR1_INITIAL_CODE_HIGH_ADDR 1
#define ADC_SAR1_SAMPLE_CYCLE_ADDR 2
#define ADC_SAR2_INITIAL_CODE_LOW_ADDR 3
#define ADC_SAR2_INITIAL_CODE_HIGH_ADDR 4
#define ADC_SAR2_SAMPLE_CYCLE_ADDR 5
#define ADC_READ_MASK(r) analog[r]
#define ADC_WRITE_MASK(r, v) (analog[r] = (v))
typedef int adc_unit_t;
typedef int adc_channel_t;
typedef int adc_atten_t;
#define ADC_UNIT_1 0
#define ADC_UNIT_2 1
#define ADC_LL_POWER_SW_ON 1
#define ADC_LL_SAR_CLK_DIV_DEFAULT(u) 1
#define ADC_LL_CTRL_RTC 0
#define ADC_LL_EVENT_ADC1_ONESHOT_DONE 1
#define ADC_LL_EVENT_ADC2_ONESHOT_DONE 2
static void gpio_ll_func_sel(int *g, int p, int f) {
    (void)g;
    (void)p;
    (void)f;
    pad_calls++;
}
static void gpio_ll_output_disable(int *g, int p) {
    (void)g;
    (void)p;
    pad_calls++;
}
#define gpio_ll_input_disable gpio_ll_output_disable
#define gpio_ll_pullup_dis gpio_ll_output_disable
#define gpio_ll_pulldown_dis gpio_ll_output_disable
static void adc_ll_enable_bus_clock(bool b) {
    HP_SYS_CLKRST.soc_clk_ctrl2.val = b;
    HP_SYS_CLKRST.peri_clk_ctrl23.val = b;
}
static void adc_ll_set_power_manage(int u, int p) {
    (void)u;
    ADC.ctrl_reg.val = p;
    LP_ADC.force_wpd_sar.val = p;
}
static void adc_ll_set_sar_clk_div(int u, int d) {
    if (u == 0)
        LP_ADC.reader1_ctrl.val = d;
    else
        LP_ADC.reader2_ctrl.val = d;
}
static void adc_oneshot_ll_output_invert(int u, bool b) {
    (void)u;
    (void)b;
}
static void adc_oneshot_ll_set_atten(int u, int c, int a) {
    (void)c;
    if (u == 0)
        LP_ADC.atten1.val = a;
    else
        LP_ADC.atten2.val = a;
}
static void adc_oneshot_ll_set_channel(int u, int c) {
    if (u == 0)
        LP_ADC.meas1_ctrl2.val = c;
    else
        LP_ADC.meas2_ctrl2.val = c;
}
static void adc_ll_set_controller(int u, int c) {
    if (u == 0)
        LP_ADC.meas1_mux.val = c;
    else
        LP_ADC.meas2_mux.val = c;
}
static void esp_rom_delay_us(unsigned n) { ticks += n; }
static void adc_oneshot_ll_start(int u) {
    (void)u;
    starts++;
}
static bool adc_oneshot_ll_get_event(unsigned e) {
    (void)e;
    return done;
}
static uint32_t adc_oneshot_ll_get_raw_result(int u) {
    (void)u;
    return raw_result;
}
static uint64_t hal_time_now_us(void) {
    ticks += 100;
    return ticks;
}
/* NATIVE_ADC */
int main(void) {
    for (unsigned unit = 1; unit <= 2; unit++)
        for (unsigned mode = 0; mode < 3; mode++) {
            memset(&HP_SYS_CLKRST, 0x11, sizeof HP_SYS_CLKRST);
            memset(&LPPERI, 0x22, sizeof LPPERI);
            memset(&ADC, 0x33, sizeof ADC);
            memset(&LP_ADC, 0x44, sizeof LP_ADC);
            memset(analog, 0x55, sizeof analog);
            pmu = 0x66;
            uint8_t clocks[sizeof HP_SYS_CLKRST], lp[sizeof LP_ADC];
            memcpy(clocks, &HP_SYS_CLKRST, sizeof clocks);
            memcpy(lp, &LP_ADC, sizeof lp);
            ticks = 0;
            done = mode != 1;
            raw_result = mode == 2 ? 5000 : 2048;
            uint16_t output = 99;
            int rc = adc_native_read(unit, 1, 3, &output);
            assert(rc == (mode == 0 ? 0 : mode == 1 ? ERR_TIMEOUT : ERR_IO));
            assert(output == (mode == 0 ? 2048 : 99));
            assert(!memcmp(clocks, &HP_SYS_CLKRST, sizeof clocks) &&
                   !memcmp(lp, &LP_ADC, sizeof lp));
            assert(ADC.ctrl_reg.val == 0x33333333 && LPPERI.clk_en.val == 0x22222222 &&
                   pmu == 0x66);
            for (unsigned i = 0; i < 6; i++)
                assert(analog[i] == 0x55);
            assert(ticks < 1500);
        }
    assert(starts == 6 && pad_calls == 30);
    puts("PASS native ADC success/timeout/invalid result restore unit, power, clock and analog "
         "register state");
}
