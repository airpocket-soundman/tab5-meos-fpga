#pragma once
#include <m5os/types.h>

/* Write-only 8-bit command/data bus to the LCD controller. */
void hal_lcd_bus_init(void);
void hal_lcd_begin(void);                    /* assert CS   */
void hal_lcd_end(void);                      /* release CS  */
void hal_lcd_write_cmd(uint8_t cmd);         /* D/C low for this byte only */
void hal_lcd_write_data(uint8_t data);       /* D/C high */
void hal_lcd_write_pixel(uint16_t rgb565);   /* two data bytes, MSB first */
void hal_lcd_write_pixels(uint16_t rgb565, uint32_t count);
void hal_delay_ms(uint32_t ms);
