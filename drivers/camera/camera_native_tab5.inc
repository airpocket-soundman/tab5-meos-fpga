/* SPDX-License-Identifier: Apache-2.0
 * Register sequencing adapted from Espressif ESP-IDF v5.4.4 camera CSI, ISP
 * and DW-GDMA drivers (Espressif Systems, 2023-2025). No RTOS driver calls.
 * DW-GDMA channel 3 is statically reserved here; LCD channel 0 is untouched.
 */
#include "esp_rom_gpio.h"
#include "hal/dw_gdma_ll.h"
#include "hal/gpio_ll.h"
#include "hal/ledc_ll.h"
#include "hal/mipi_csi_hal.h"
#include "hal/mipi_csi_ll.h"
#include "soc/gpio_sig_map.h"
#include "soc/io_mux_reg.h"

#define CAMERA_DMA_CH 3
#define CAMERA_DMA_ERRORS                                                                          \
    (DW_GDMA_LL_CHANNEL_EVENT_SRC_DEC_ERR | DW_GDMA_LL_CHANNEL_EVENT_DST_DEC_ERR |                 \
     DW_GDMA_LL_CHANNEL_EVENT_SRC_SLV_ERR | DW_GDMA_LL_CHANNEL_EVENT_DST_SLV_ERR |                 \
     DW_GDMA_LL_CHANNEL_EVENT_ABORTED)
static mipi_csi_hal_context_t s_csi;
static bool s_native_ready, s_dma_pending;
static uint64_t s_dma_started;

static bool xclk_start(void) {
    uint32_t irq = arch_irq_disable();
    int __DECLARE_RCC_ATOMIC_ENV = 0;
    ledc_ll_enable_bus_clock(true);
    ledc_ll_enable_clock(&LEDC, true);
    ledc_ll_set_slow_clk_sel(&LEDC, LEDC_SLOW_CLK_PLL_DIV);
    ledc_ll_timer_pause(&LEDC, LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
    ledc_ll_set_clock_divider(&LEDC, LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, 427);
    ledc_ll_set_duty_resolution(&LEDC, LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, 1);
    ledc_ll_ls_timer_update(&LEDC, LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
    ledc_ll_timer_rst(&LEDC, LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
    ledc_ll_bind_channel_timer(&LEDC, LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, LEDC_TIMER_0);
    ledc_ll_set_hpoint(&LEDC, LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_ll_set_duty_int_part(&LEDC, LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 1);
    ledc_ll_set_fade_param_range(&LEDC, LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0, 1, 1, 0, 1);
    ledc_ll_set_range_number(&LEDC, LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 1);
    ledc_ll_set_duty_start(&LEDC, LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    ledc_ll_set_sig_out_en(&LEDC, LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, true);
    ledc_ll_ls_channel_update(&LEDC, LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    gpio_ll_func_sel(&GPIO, CAM_XCLK_PIN, PIN_FUNC_GPIO);
    gpio_ll_output_enable(&GPIO, CAM_XCLK_PIN);
    esp_rom_gpio_connect_out_signal(CAM_XCLK_PIN, LEDC_LS_SIG_OUT_PAD_OUT0_IDX, false, false);
    ledc_ll_timer_resume(&LEDC, LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
    ledc_ll_ls_timer_update(&LEDC, LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
    arch_irq_restore(irq);
    return true; /* 80 MHz / (427/256 * 2) = 23.981 MHz. */
}
static void xclk_stop(void) {
    uint32_t irq = arch_irq_disable();
    ledc_ll_set_sig_out_en(&LEDC, LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, false);
    ledc_ll_ls_channel_update(&LEDC, LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    ledc_ll_timer_pause(&LEDC, LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
    arch_irq_restore(irq);
}
static bool native_camera_init(void) {
    dw_gdma_dev_t *dma = DW_GDMA_LL_GET_HW(0);
    /* Never reset the global DW controller: the display is already using it. */
    if (dma->chen0.val & (1u << CAMERA_DMA_CH)) {
        ksnprintf(s_error, sizeof(s_error), "DW-GDMA channel 3 is busy");
        return false;
    }
    uint32_t irq = arch_irq_disable();
    int __DECLARE_RCC_ATOMIC_ENV = 0;
    mipi_csi_ll_enable_host_bus_clock(0, true);
    mipi_csi_ll_reset_host_clock(0);
    mipi_csi_ll_enable_brg_module_clock(0, true);
    mipi_csi_ll_reset_brg_module_clock(0);
    mipi_csi_brg_ll_enable_clock(MIPI_CSI_BRG_LL_GET_HW(0), true);
    mipi_csi_ll_set_phy_clock_source(0, MIPI_CSI_PHY_CLK_SRC_DEFAULT);
    mipi_csi_ll_enable_phy_config_clock(0, false);
    mipi_csi_ll_enable_phy_config_clock(0, true);
    mipi_csi_hal_config_t cfg = {.frame_height = CAMERA_WIDTH,
                                 .frame_width = CAMERA_HEIGHT,
                                 .lane_bit_rate_mbps = CAM_LANE_MBPS,
                                 .lanes_num = 1,
                                 .byte_swap_en = false};
    mipi_csi_hal_init(&s_csi, &cfg);
    mipi_csi_brg_ll_set_burst_len(s_csi.bridge_dev, 512);
    isp_ll_enable_module_clock(ISP_LL_GET_HW(0), true);
    isp_ll_reset_module_clock(ISP_LL_GET_HW(0));
    isp_hal_init(&s_isp_context, 0);
    isp_ll_select_clk_source(s_isp_context.hw, ISP_CLK_SRC_PLL240);
    hal_utils_clk_div_t div = {.integer = 2};
    isp_ll_set_clock_div(s_isp_context.hw, &div);
    isp_ll_clk_enable(s_isp_context.hw, true);
    color_space_pixel_format_t in = {.color_type_id =
                                         COLOR_TYPE_ID(COLOR_SPACE_RAW, COLOR_PIXEL_RAW8)};
    color_space_pixel_format_t out = {.color_type_id =
                                          COLOR_TYPE_ID(COLOR_SPACE_RGB, COLOR_PIXEL_RGB565)};
    bool ok = isp_ll_set_input_data_color_format(s_isp_context.hw, in) &&
              isp_ll_set_output_data_color_format(s_isp_context.hw, out);
    isp_ll_set_input_data_source(s_isp_context.hw, ISP_INPUT_DATA_SOURCE_CSI);
    isp_ll_enable_line_start_packet_exist(s_isp_context.hw, false);
    isp_ll_enable_line_end_packet_exist(s_isp_context.hw, false);
    isp_ll_set_intput_data_h_pixel_num(s_isp_context.hw, CAMERA_WIDTH);
    isp_ll_set_intput_data_v_row_num(s_isp_context.hw, CAMERA_HEIGHT);
    isp_ll_set_bayer_mode(s_isp_context.hw, COLOR_RAW_ELEMENT_ORDER_BGGR);
    isp_ll_yuv_set_std(s_isp_context.hw, ISP_YUV_CONV_STD_BT601);
    isp_ll_shadow_set_mode(s_isp_context.hw, ISP_SHADOW_MODE_UPDATE_ONLY_NEXT_VSYNC);
    isp_ll_enable(s_isp_context.hw, true);
    /* LCD initializes this shared controller at boot. Only enable clocks, never reset. */
    dw_gdma_ll_enable_bus_clock(0, true);
    dw_gdma_ll_enable_controller(dma, true);
    dw_gdma_ll_channel_enable_intr_propagation(dma, CAMERA_DMA_CH, UINT32_MAX, false);
    dw_gdma_ll_channel_enable_intr_generation(dma, CAMERA_DMA_CH, UINT32_MAX, true);
    dw_gdma_ll_channel_clear_intr(dma, CAMERA_DMA_CH, UINT32_MAX);
    dw_gdma_ll_channel_set_trans_flow(dma, CAMERA_DMA_CH, DW_GDMA_ROLE_PERIPH_CSI, DW_GDMA_ROLE_MEM,
                                      DW_GDMA_FLOW_CTRL_SRC);
    dw_gdma_ll_channel_set_src_multi_block_type(dma, CAMERA_DMA_CH,
                                                DW_GDMA_BLOCK_TRANSFER_CONTIGUOUS);
    dw_gdma_ll_channel_set_dst_multi_block_type(dma, CAMERA_DMA_CH,
                                                DW_GDMA_BLOCK_TRANSFER_CONTIGUOUS);
    dw_gdma_ll_channel_set_src_handshake_interface(dma, CAMERA_DMA_CH, DW_GDMA_HANDSHAKE_HW);
    dw_gdma_ll_channel_set_dst_handshake_interface(dma, CAMERA_DMA_CH, DW_GDMA_HANDSHAKE_HW);
    dw_gdma_ll_channel_set_src_handshake_periph(dma, CAMERA_DMA_CH, DW_GDMA_ROLE_PERIPH_CSI);
    dw_gdma_ll_channel_set_priority(dma, CAMERA_DMA_CH, 1);
    dw_gdma_ll_channel_set_src_outstanding_limit(dma, CAMERA_DMA_CH, 5);
    dw_gdma_ll_channel_set_dst_outstanding_limit(dma, CAMERA_DMA_CH, 5);
    dw_gdma_ll_channel_set_src_periph_status_addr(dma, CAMERA_DMA_CH, MIPI_CSI_BRG_MEM_BASE);
    dw_gdma_ll_channel_set_dst_periph_status_addr(dma, CAMERA_DMA_CH, 0);
    s_native_ready = true;
    arch_irq_restore(irq);
    return ok;
}
static bool native_camera_start(void *frame) {
    if (!s_native_ready || s_dma_pending)
        return false;
    dw_gdma_dev_t *d = DW_GDMA_LL_GET_HW(0);
    unsigned c = CAMERA_DMA_CH;
    if (d->chen0.val & (1u << c))
        return false;
    esp_cache_msync(frame, FB_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    dw_gdma_ll_channel_clear_intr(d, c, UINT32_MAX);
    dw_gdma_ll_channel_set_src_addr(d, c, MIPI_CSI_BRG_MEM_BASE);
    dw_gdma_ll_channel_set_dst_addr(d, c, (uintptr_t)frame);
    dw_gdma_ll_channel_set_trans_block_size(d, c, CAMERA_WIDTH * CAMERA_HEIGHT / 8);
    dw_gdma_ll_channel_set_src_master_port(d, c, MIPI_CSI_BRG_MEM_BASE);
    dw_gdma_ll_channel_set_dst_master_port(d, c, (uintptr_t)frame);
    dw_gdma_ll_channel_set_src_trans_width(d, c, DW_GDMA_TRANS_WIDTH_64);
    dw_gdma_ll_channel_set_dst_trans_width(d, c, DW_GDMA_TRANS_WIDTH_64);
    dw_gdma_ll_channel_set_src_burst_items(d, c, DW_GDMA_BURST_ITEMS_512);
    dw_gdma_ll_channel_set_dst_burst_items(d, c, DW_GDMA_BURST_ITEMS_512);
    dw_gdma_ll_channel_set_src_burst_mode(d, c, DW_GDMA_BURST_MODE_FIXED);
    dw_gdma_ll_channel_set_dst_burst_mode(d, c, DW_GDMA_BURST_MODE_INCREMENT);
    dw_gdma_ll_channel_set_src_burst_len(d, c, 16);
    dw_gdma_ll_channel_set_dst_burst_len(d, c, 16);
    dw_gdma_ll_channel_enable_src_periph_status_write_back(d, c, false);
    dw_gdma_ll_channel_enable_dst_periph_status_write_back(d, c, false);
    dw_gdma_ll_channel_enable(d, c, true);
    mipi_csi_brg_ll_enable(s_csi.bridge_dev, true);
    s_dma_started = hal_time_now_us();
    s_dma_pending = true;
    return true;
}
static bool native_camera_stop(void) {
    if (!s_native_ready)
        return true;
    mipi_csi_brg_ll_enable(s_csi.bridge_dev, false);
    dw_gdma_dev_t *d = DW_GDMA_LL_GET_HW(0);
    dw_gdma_ll_channel_enable(d, CAMERA_DMA_CH, false);
    uint64_t deadline = hal_time_now_us() + 1000;
    while ((d->chen0.val & (1u << CAMERA_DMA_CH)) && hal_time_now_us() < deadline) {
    }
    if (d->chen0.val & (1u << CAMERA_DMA_CH)) {
        /* Flow-controlled CSI can wait forever after the sensor stops. Abort
         * only this channel, with a deadline instead of the LL's endless loop. */
        d->chen1.val = 0x101u << CAMERA_DMA_CH;
        deadline = hal_time_now_us() + 20000;
        while ((d->chen0.val | d->chen1.val) & (1u << CAMERA_DMA_CH)) {
            if (hal_time_now_us() >= deadline)
                return false;
        }
    }
    dw_gdma_ll_channel_clear_intr(d, CAMERA_DMA_CH, UINT32_MAX);
    s_dma_pending = false;
    return true;
}
static int native_camera_poll(void) {
    if (!s_dma_pending)
        return 0;
    dw_gdma_dev_t *d = DW_GDMA_LL_GET_HW(0);
    uint32_t events = dw_gdma_ll_channel_get_intr_status(d, CAMERA_DMA_CH);
    if (events & CAMERA_DMA_ERRORS)
        return -1;
    if (events & DW_GDMA_LL_CHANNEL_EVENT_DMA_TFR_DONE) {
        if (d->chen0.val & (1u << CAMERA_DMA_CH))
            return hal_time_now_us() - s_dma_started > 1000000 ? -1 : 0;
        dw_gdma_ll_channel_clear_intr(d, CAMERA_DMA_CH, events);
        mipi_csi_brg_ll_enable(s_csi.bridge_dev, false);
        s_dma_pending = false;
        return 1;
    }
    return hal_time_now_us() - s_dma_started > 1000000 ? -1 : 0;
}
static void native_camera_deinit(void) {
    if (!s_native_ready)
        return;
    uint32_t irq = arch_irq_disable();
    int __DECLARE_RCC_ATOMIC_ENV = 0;
    dw_gdma_ll_channel_enable_intr_generation(DW_GDMA_LL_GET_HW(0), CAMERA_DMA_CH, UINT32_MAX,
                                              false);
    isp_ll_enable(s_isp_context.hw, false);
    isp_ll_clk_enable(s_isp_context.hw, false);
    isp_ll_enable_module_clock(s_isp_context.hw, false);
    mipi_csi_ll_enable_phy_config_clock(0, false);
    mipi_csi_ll_enable_host_bus_clock(0, false);
    mipi_csi_ll_enable_brg_module_clock(0, false);
    s_native_ready = false;
    arch_irq_restore(irq);
}
