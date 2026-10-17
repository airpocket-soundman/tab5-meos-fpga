/*
 * ST7789 init for the Cardputer's 135x240 panel, used in landscape (240x135).
 *
 * Everything here is transcribed from M5GFX (Panel_ST7789.hpp init list; Panel_LCD.hpp/.inl for
 * COLMOD, the MADCTL table and the window offsets; M5GFX.cpp "Cardputer" block: panel 135x240,
 * offset_x 52, offset_y 40, rotation 1, invert true).
 *
 * Landscape window offsets, derived the way Panel_LCD::setRotation does for rotation 1:
 *   offsets are swapped for an odd rotation      -> ox = 40, oy = 52
 *   panel/memory sizes are swapped               -> pw = 240, ph = 135, mw = 320, mh = 240
 *   colstart = ox                                -> 40
 *   rowstart = mh - (ph + oy) = 240 - (135 + 52) -> 53
 * MADCTL for rotation 1 = MV|MX|MH (0x64) | BGR (0x08) = 0x6C.
 */
#include "panel.h"
#include <m5os/hal_lcd_bus.h>

#define CMD_SLPOUT   0x11
#define CMD_INVON    0x21
#define CMD_DISPON   0x29
#define CMD_CASET    0x2A
#define CMD_RASET    0x2B
#define CMD_RAMWR    0x2C
#define CMD_MADCTL   0x36
#define CMD_IDMOFF   0x38
#define CMD_COLMOD   0x3A

#define COLMOD_RGB565    0x55
#define MADCTL_LANDSCAPE 0x6C
#define SLPOUT_DELAY_MS  130
#define COL_OFFSET       40
#define ROW_OFFSET       53
#define LIST_END         0xFF

static const uint8_t init_list[] = {
    0xB7, 1, 0x35,                                  /* GCTRL */
    0xBB, 1, 0x28,                                  /* VCOMS (JLX240 datasheet value) */
    0xC0, 1, 0x0C,                                  /* LCMCTRL */
    0xC2, 2, 0x01, 0xFF,                            /* VDVVRHEN */
    0xC3, 1, 0x10,                                  /* VRHS */
    0xC4, 1, 0x20,                                  /* VDVSET */
    0xD0, 2, 0xA4, 0xA1,                            /* PWCTRL1 */
    0xB0, 2, 0x00, 0xC0,                            /* RAMCTRL */
    0xE0, 14, 0xD0,0x00,0x02,0x07,0x0A,0x28,0x32,0x44,0x42,0x06,0x0E,0x12,0x14,0x17,   /* gamma + */
    0xE1, 14, 0xD0,0x00,0x02,0x07,0x0A,0x28,0x31,0x54,0x47,0x0E,0x1C,0x17,0x1B,0x1E,   /* gamma - */
    LIST_END, LIST_END,
};

void panel_init(void)
{
    hal_lcd_begin();
    const uint8_t *p = init_list;
    while (!(p[0] == LIST_END && p[1] == LIST_END)) {
        uint8_t cmd = *p++, n = *p++;
        hal_lcd_write_cmd(cmd);
        for (uint8_t i = 0; i < n; i++) hal_lcd_write_data(*p++);
    }
    hal_lcd_write_cmd(CMD_SLPOUT);
    hal_lcd_end();
    hal_delay_ms(SLPOUT_DELAY_MS);

    hal_lcd_begin();
    hal_lcd_write_cmd(CMD_IDMOFF);
    hal_lcd_write_cmd(CMD_DISPON);
    hal_lcd_write_cmd(CMD_COLMOD);  hal_lcd_write_data(COLMOD_RGB565);
    hal_lcd_write_cmd(CMD_MADCTL);  hal_lcd_write_data(MADCTL_LANDSCAPE);
    hal_lcd_write_cmd(CMD_INVON);                    /* this panel is wired inverted (cfg.invert = true) */
    hal_lcd_end();
}

void panel_set_window(int x0, int y0, int x1, int y1)
{
    x0 += COL_OFFSET; x1 += COL_OFFSET;
    y0 += ROW_OFFSET; y1 += ROW_OFFSET;
    hal_lcd_write_cmd(CMD_CASET);
    hal_lcd_write_data((uint8_t)(x0 >> 8)); hal_lcd_write_data((uint8_t)x0);
    hal_lcd_write_data((uint8_t)(x1 >> 8)); hal_lcd_write_data((uint8_t)x1);
    hal_lcd_write_cmd(CMD_RASET);
    hal_lcd_write_data((uint8_t)(y0 >> 8)); hal_lcd_write_data((uint8_t)y0);
    hal_lcd_write_data((uint8_t)(y1 >> 8)); hal_lcd_write_data((uint8_t)y1);
    hal_lcd_write_cmd(CMD_RAMWR);
}
