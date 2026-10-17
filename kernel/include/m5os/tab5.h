#pragma once
/* Tab5-only board facts shared between the HAL (hal/esp32p4) and the drivers. */

typedef enum { TAB5_PANEL_UNKNOWN = 0, TAB5_PANEL_ILI9881C, TAB5_PANEL_ST7123, TAB5_PANEL_ST7121 } tab5_panel_t;

#define TAB5_GT911_ADDR_ALT     0x14     /* GT911 touch controller (ILI9881C units) */
#define TAB5_ST7123_TOUCH_ADDR  0x55     /* touch controller of the ST7123/ST7121 units */
#define TAB5_ST7123_FW_REG      0x0000   /* its firmware-version register: 1 on ST7121 units, 3 on ST7123 units */

tab5_panel_t tab5_panel(void);           /* valid after board_init() */
