/*
 * Tab5 display: 5" 720x1280 ILI9881C panel behind a 2-lane MIPI-DSI link, used in landscape (1280x720).
 *
 * How this differs from the SPI panels of the other boards (drivers/lcd/display.c):
 *  - There is a FRAMEBUFFER (1.8 MB RGB565, in PSRAM).  The DSI host's DMA scans it out to the panel
 *    continuously, so drawing = writing memory; nothing is "sent" per drawing call.
 *  - The CPU's data cache sits between our writes and the DMA's reads (the DMA reads PSRAM, not the
 *    cache), so every drawing call ends by writing the touched cache lines back (esp_cache_msync).
 *  - The panel is physically portrait.  Logical (x, y) in 1280x720 is stored at physical column
 *    y, row (DISPLAY_WIDTH-1 - x), i.e. the picture is rotated 90 degrees when it is written.
 *
 * Bring-up (power the DSI PHY, start the DSI bus, send the panel's init commands over DSI, start the
 * DPI video stream) uses ESP-IDF's esp_lcd DSI driver ONCE at boot, before the scheduler runs, and
 * then never again: after display_init() returns, scan-out is pure hardware.  This is the one place
 * where the kernel leans on an IDF driver for more than register headers, because the DSI PHY/host
 * bring-up is hundreds of registers of timing that has nothing to teach about an OS.
 * Panel-specific values (init sequence, video timing) are from M5Stack's board support package.
 */
#include <m5os/display.h>
#include <m5os/board.h>
#include <m5os/hal_gpio.h>
#include <m5os/hal_mem.h>
#include <m5os/kprintf.h>
#include <m5os/tab5.h>
#include "font8x8.h"
#include "ili9881c_tab5_init.h"
#include "st7121_tab5_init.h"

#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_heap_caps_init.h"

#define PANEL_W            720          /* physical, portrait */
#define PANEL_H            1280
#define BYTES_PER_PIXEL    2
#define CACHE_LINE         64u          /* ESP32-P4 L2 cache line */
#define GLYPH_SIZE         8

#define DSI_LANES          2
#define DSI_PHY_LDO_CHAN   3            /* LDO_VO3 feeds VDD_MIPI_DPHY */
#define DSI_PHY_LDO_MV     2500
#define FIRST_FRAMES_MS    200
#define SPLASH_MS          800
#define SWRESET_MS         20           /* vendor BSP waits this long after the software reset */
#define ST7121_RESET_MS    120          /* vendor BSP: wait after the software reset */
#define ST7121_CMD_GAP_MS  5            /* ... and between init commands */
#define SLEEP_OUT_MS       120          /* ILI9881C datasheet: wait after SLPOUT before further commands */

#define BACKLIGHT_PIN      22

#define ILI_CMD_PAGE_SELECT 0xFF
#define ILI_PAD_CONTROL     0xB7
#define ILI_2_LANE          0x03
#define COLMOD_RGB565       0x55

/* What differs between the two panels this board has shipped with.  Link speed and video timing are
 * the vendor BSP's values for each. */
typedef struct {
    const char *name;
    uint32_t lane_mbps, dpi_mhz;
    esp_lcd_video_timing_t timing;
} panel_info_t;

static const panel_info_t PANEL_ILI9881C = {
    "ILI9881C", 730, 60,
    { .h_size = PANEL_W, .v_size = PANEL_H, .hsync_back_porch = 140, .hsync_pulse_width = 40, .hsync_front_porch = 40,
      .vsync_back_porch = 20, .vsync_pulse_width = 4, .vsync_front_porch = 20 },
};
static const panel_info_t PANEL_ST7121 = {
    "ST7121", 965, 70,
    { .h_size = PANEL_W, .v_size = PANEL_H, .hsync_back_porch = 40, .hsync_pulse_width = 2, .hsync_front_porch = 40,
      .vsync_back_porch = 24, .vsync_pulse_width = 20, .vsync_front_porch = 200 },
};

static volatile uint32_t s_frames;       /* refresh-done interrupts seen: proves the scan-out loop runs */
static uint16_t *s_fb;                  /* physical layout: PANEL_H rows of PANEL_W pixels */
static esp_lcd_panel_io_handle_t s_io;

static bool on_refresh_done(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *ev, void *ctx)
{
    (void)panel; (void)ev; (void)ctx;
    s_frames++;
    return false;
}

static void delay_ms(uint32_t ms) { while (ms--) hal_delay_us(1000); }

static int s_tx_failed;

static bool tx(uint8_t cmd, const uint8_t *data, size_t len)
{
    bool ok = esp_lcd_panel_io_tx_param(s_io, cmd, data, len) == ESP_OK;
    if (!ok) s_tx_failed++;
    return ok;
}

/* Write the cache lines covering physical rows [row0, row1] back to PSRAM so the DMA sees them. */
static void fb_flush_rows(int row0, int row1)
{
    uintptr_t a = (uintptr_t)s_fb + (uintptr_t)row0 * PANEL_W * BYTES_PER_PIXEL;
    uintptr_t b = (uintptr_t)s_fb + (uintptr_t)(row1 + 1) * PANEL_W * BYTES_PER_PIXEL;
    a &= ~(uintptr_t)(CACHE_LINE - 1);
    b = (b + CACHE_LINE - 1) & ~(uintptr_t)(CACHE_LINE - 1);
    esp_cache_msync((void *)a, b - a, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

static inline void put(int x, int y, uint16_t c) { s_fb[(size_t)(DISPLAY_WIDTH - 1 - x) * PANEL_W + y] = c; }

/* Panel start-up over DSI, before the video stream runs.  The two controllers want different sequences. */
static void panel_start_ili9881c(void)
{
    tx(LCD_CMD_SWRESET, NULL, 0);                     /* software reset: the reset line is only released, not pulsed */
    delay_ms(SWRESET_MS);
    /* Panel registers are grouped in pages; 0xFF 98 81 <n> selects page n.  The lane count lives in page 1. */
    static const uint8_t page0[] = { 0x98, 0x81, 0x00 }, page1[] = { 0x98, 0x81, 0x01 };
    static const uint8_t lanes2[] = { ILI_2_LANE }, colmod[] = { COLMOD_RGB565 }, madctl[] = { 0x00 };
    tx(ILI_CMD_PAGE_SELECT, page1, sizeof page1);
    tx(ILI_PAD_CONTROL, lanes2, 1);
    tx(ILI_CMD_PAGE_SELECT, page0, sizeof page0);
    tx(LCD_CMD_SLPOUT, NULL, 0);
    delay_ms(SLEEP_OUT_MS);
    tx(LCD_CMD_MADCTL, madctl, 1);
    tx(LCD_CMD_COLMOD, colmod, 1);
    for (size_t i = 0; i < sizeof ili9881c_tab5_init / sizeof ili9881c_tab5_init[0]; i++) {
        tx(ili9881c_tab5_init[i].cmd, ili9881c_tab5_init[i].data, ili9881c_tab5_init[i].len);
        delay_ms(ili9881c_tab5_init[i].delay_ms);
    }
}

static void panel_start_st7121(void)
{
    tx(LCD_CMD_SWRESET, NULL, 0);
    delay_ms(ST7121_RESET_MS);
    /* The table contains sleep-out and display-on with their own settle times. */
    for (size_t i = 0; i < sizeof st7121_tab5_init / sizeof st7121_tab5_init[0]; i++) {
        tx(st7121_tab5_init[i].cmd, st7121_tab5_init[i].data, st7121_tab5_init[i].len);
        delay_ms(st7121_tab5_init[i].delay_ms + ST7121_CMD_GAP_MS);
    }
}

bool display_init(void)
{
    if (!board_init()) return false;
    const panel_info_t *pi;
    switch (tab5_panel()) {
    case TAB5_PANEL_ILI9881C: pi = &PANEL_ILI9881C; break;
    case TAB5_PANEL_ST7121:   pi = &PANEL_ST7121;   break;
    default:
        kprintf("display: this Tab5 has an ST7123 panel; the driver is not written yet\n");
        return false;
    }
    kprintf("display: %s panel, %u Mbit/s per lane\n", pi->name, (unsigned)pi->lane_mbps);

    esp_ldo_channel_handle_t ldo = NULL;
    esp_ldo_channel_config_t ldo_cfg = { .chan_id = DSI_PHY_LDO_CHAN, .voltage_mv = DSI_PHY_LDO_MV };
    if (esp_ldo_acquire_channel(&ldo_cfg, &ldo) != ESP_OK) { kprintf("display: DSI PHY power failed\n"); return false; }

    esp_lcd_dsi_bus_handle_t bus = NULL;
    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id = 0, .num_data_lanes = DSI_LANES,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT, .lane_bit_rate_mbps = pi->lane_mbps,
    };
    if (esp_lcd_new_dsi_bus(&bus_cfg, &bus) != ESP_OK) { kprintf("display: DSI bus failed\n"); return false; }

    esp_lcd_dbi_io_config_t dbi_cfg = { .virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8 };
    if (esp_lcd_new_panel_io_dbi(bus, &dbi_cfg, &s_io) != ESP_OK) { kprintf("display: DBI io failed\n"); return false; }

    if (pi == &PANEL_ST7121) panel_start_st7121(); else panel_start_ili9881c();

    /* The DPI driver takes its framebuffer from the IDF heap, which is empty (the kernel owns PSRAM).
     * Hand it the reserved top slice of PSRAM, with the same capabilities IDF itself would give PSRAM. */
    uintptr_t rb;
    size_t rs;
    hal_psram_reserve_region(&rb, &rs);
    uint32_t caps[] = { MALLOC_CAP_SPIRAM | MALLOC_CAP_DEFAULT, 0, MALLOC_CAP_8BIT | MALLOC_CAP_32BIT | MALLOC_CAP_SIMD };
    if (!rs || heap_caps_add_region_with_caps(caps, (intptr_t)rb, (intptr_t)(rb + rs)) != ESP_OK) {
        kprintf("display: no PSRAM reserve for the framebuffer\n");
        return false;
    }

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_dpi_panel_config_t dpi = {
        .virtual_channel = 0, .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = pi->dpi_mhz, .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565, .num_fbs = 1,
        .video_timing = pi->timing,
    };
    if (esp_lcd_new_panel_dpi(bus, &dpi, &panel) != ESP_OK) { kprintf("display: DPI panel failed\n"); return false; }
    void *fb = NULL;
    if (esp_lcd_dpi_panel_get_frame_buffer(panel, 1, &fb) != ESP_OK || !fb) { kprintf("display: no framebuffer\n"); return false; }
    s_fb = fb;
    display_clear(COLOR_BLACK);                       /* black before the stream starts */
    esp_lcd_dpi_panel_event_callbacks_t cbs = { .on_refresh_done = on_refresh_done };
    esp_lcd_dpi_panel_register_event_callbacks(panel, &cbs, NULL);
    if (esp_lcd_panel_init(panel) != ESP_OK) { kprintf("display: DPI start failed\n"); return false; }
    tx(LCD_CMD_DISPON, NULL, 0);
    delay_ms(FIRST_FRAMES_MS);                        /* let a few frames go out, then check the refresh interrupt ran */
    kprintf("display: %u refresh interrupts, %d panel command(s) failed\n", (unsigned)s_frames, s_tx_failed);
    board_backlight(255);

    /* Boot splash: colour bars (red, green, blue, white), one per quarter of the width.  They also tell the
     * person at the board - who is the only one who can see the screen - whether scan-out works at all. */
    static const uint16_t bars[] = { COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_WHITE };
    for (int i = 0; i < 4; i++) display_fill_rect(i * (DISPLAY_WIDTH / 4), 0, DISPLAY_WIDTH / 4, DISPLAY_HEIGHT, bars[i]);
    delay_ms(SPLASH_MS);
    display_clear(COLOR_BLACK);
    return true;
}

void display_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > DISPLAY_WIDTH)  w = DISPLAY_WIDTH  - x;
    if (y + h > DISPLAY_HEIGHT) h = DISPLAY_HEIGHT - y;
    if (w <= 0 || h <= 0 || !s_fb) return;
    for (int i = 0; i < w; i++)
        for (int j = 0; j < h; j++) put(x + i, y + j, color);
    fb_flush_rows(DISPLAY_WIDTH - x - w, DISPLAY_WIDTH - 1 - x);
}

void display_clear(uint16_t color)
{
    display_fill_rect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, color);
}

void display_draw_rect(int x, int y, int w, int h, uint16_t color)
{
    display_fill_rect(x, y, w, 1, color);
    display_fill_rect(x, y + h - 1, w, 1, color);
    display_fill_rect(x, y, 1, h, color);
    display_fill_rect(x + w - 1, y, 1, h, color);
}

static void glyph_at(int x, int y, char c, uint16_t fg, uint16_t bg, int scale)
{
    if (c < FONT8X8_FIRST || c > FONT8X8_LAST) c = '?';
    const uint8_t *g = font8x8[c - FONT8X8_FIRST];
    int size = GLYPH_SIZE * scale;
    for (int row = 0; row < size; row++) {
        uint8_t bits = g[row / scale];
        for (int col = 0; col < size; col++) put(x + col, y + row, ((bits >> (col / scale)) & 1) ? fg : bg);
    }
}

void display_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, int scale)
{
    if (scale < 1) scale = 1;
    int size = GLYPH_SIZE * scale;
    if (!s_fb || x < 0 || y < 0 || x + size > DISPLAY_WIDTH || y + size > DISPLAY_HEIGHT) return;
    glyph_at(x, y, c, fg, bg, scale);
    fb_flush_rows(DISPLAY_WIDTH - x - size, DISPLAY_WIDTH - 1 - x);
}

void display_draw_text(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale)
{
    if (scale < 1) scale = 1;
    int cell = GLYPH_SIZE * scale;
    if (!s_fb || x < 0 || y < 0 || y + cell > DISPLAY_HEIGHT) return;
    int len = 0;
    while (s[len] && x + (len + 1) * cell <= DISPLAY_WIDTH) len++;
    if (len == 0) return;
    for (int i = 0; i < len; i++) glyph_at(x + i * cell, y, s[i], fg, bg, scale);
    fb_flush_rows(DISPLAY_WIDTH - x - len * cell, DISPLAY_WIDTH - 1 - x);
}

void display_draw_bitmap(int x, int y, int w, int h, const uint16_t *px)
{
    if (!s_fb || x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > DISPLAY_WIDTH || y + h > DISPLAY_HEIGHT) return;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) put(x + i, y + j, px[j * w + i]);
    fb_flush_rows(DISPLAY_WIDTH - x - w, DISPLAY_WIDTH - 1 - x);
}
