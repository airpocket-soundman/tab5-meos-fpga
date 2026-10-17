/*
 * ILI9342 init for the CoreS3 panel.  Command lists, MADCTL, COLMOD and the
 * INVON setting are transcribed from M5GFX (Panel_ILI9342.hpp, Panel_LCD.hpp /
 * Panel_LCD.inl, M5GFX.cpp "Panel_M5StackCoreS3": invert=true,
 * offset_rotation=3, default rotation 1 -> internal rotation 0 -> MADCTL = BGR).
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
#define MADCTL_BGR       0x08
#define MADCTL_ROTATION0 0x00      /* internal rotation 0: 320 wide, 240 tall */
#define SLPOUT_DELAY_MS  120

#define LIST_END         0xFF      /* {0xFF, 0xFF} terminates a list (as in M5GFX) */

static void run_list(const uint8_t *p)
{
    while (!(p[0] == LIST_END && p[1] == LIST_END)) {
        uint8_t cmd = *p++;
        uint8_t n   = *p++;
        hal_lcd_write_cmd(cmd);
        for (uint8_t i = 0; i < n; i++) hal_lcd_write_data(*p++);
    }
}

#ifndef LCD_PANEL_ILI9342E
/* ILI9342C (Panel_ILI9342::getInitCommands) minus the final SLPOUT, which is issued below. */
static const uint8_t init_list[] = {
    0xC8, 3, 0xFF, 0x93, 0x42,                 /* SETEXTC: enable extended commands */
    0xC0, 2, 0x12, 0x12,                       /* PWCTR1 */
    0xC1, 1, 0x03,                             /* PWCTR2 */
    0xC5, 1, 0xF2,                             /* VMCTR1 */
    0xB0, 1, 0xE0,
    0xF6, 3, 0x01, 0x00, 0x00,
    0xE0, 15, 0x00,0x0C,0x11,0x04,0x11,0x08,0x37,0x89,0x4C,0x06,0x0C,0x0A,0x2E,0x34,0x0F, /* gamma + */
    0xE1, 15, 0x00,0x0B,0x11,0x05,0x13,0x09,0x33,0x67,0x48,0x07,0x0E,0x0B,0x2E,0x33,0x0F, /* gamma - */
    0xB6, 4, 0x08, 0x82, 0x1D, 0x04,           /* DFUNCTR */
    CMD_IDMOFF, 0,
    CMD_DISPON, 0,
    LIST_END, LIST_END,
};
#else
/* ILI9342E (Panel_ILI9342E::getInitCommands). */
static const uint8_t init_list[] = {
    0xDD, 1, 0x01,
    0xD5, 1, 0x00,
    0xB1, 1, 0x22,
    0xC8, 1, 0x38,
    0xCB, 1, 0x1C,
    0xC9, 1, 0x1A,
    0xCA, 1, 0x1A,
    0xB7, 4, 0x5A, 0x41, 0x11, 0x19,
    0xE4, 15, 0x04,0x08,0x11,0x06,0x12,0x07,0x3A,0x76,0x47,0x07,0x0F,0x0A,0x11,0x19,0x05,
    0xE5, 15, 0x02,0x03,0x07,0x06,0x12,0x07,0x36,0x5F,0x48,0x06,0x10,0x0C,0x16,0x14,0x09,
    CMD_IDMOFF, 0,
    CMD_DISPON, 0,
    LIST_END, LIST_END,
};
#endif

void panel_init(void)
{
    hal_lcd_begin();
    run_list(init_list);
    hal_lcd_write_cmd(CMD_SLPOUT);
    hal_lcd_end();
    hal_delay_ms(SLPOUT_DELAY_MS);              /* controller needs 120 ms after SLPOUT */

    hal_lcd_begin();
    hal_lcd_write_cmd(CMD_COLMOD);  hal_lcd_write_data(COLMOD_RGB565);
    hal_lcd_write_cmd(CMD_MADCTL);  hal_lcd_write_data(MADCTL_ROTATION0 | MADCTL_BGR);
    hal_lcd_write_cmd(CMD_INVON);                /* this panel is wired inverted */
    hal_lcd_end();
}

void panel_set_window(int x0, int y0, int x1, int y1)
{
    hal_lcd_write_cmd(CMD_CASET);
    hal_lcd_write_data((uint8_t)(x0 >> 8)); hal_lcd_write_data((uint8_t)x0);
    hal_lcd_write_data((uint8_t)(x1 >> 8)); hal_lcd_write_data((uint8_t)x1);
    hal_lcd_write_cmd(CMD_RASET);
    hal_lcd_write_data((uint8_t)(y0 >> 8)); hal_lcd_write_data((uint8_t)y0);
    hal_lcd_write_data((uint8_t)(y1 >> 8)); hal_lcd_write_data((uint8_t)y1);
    hal_lcd_write_cmd(CMD_RAMWR);
}
