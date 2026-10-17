#pragma once
#include <m5os/types.h>

/* SD card SPI pins on the shared CoreS3 bus (bit-banged, mode 0).  The caller
 * holds spi_bus_lock() around begin..end. */
void    hal_sdspi_begin(void);                /* take the pins: MISO becomes an input */
void    hal_sdspi_end(void);                  /* give them back to the LCD (D/C output) */
void    hal_sdspi_cs(bool selected);
void    hal_sdspi_set_slow(bool slow);        /* slow ~200 kHz for card init, else full speed */
uint8_t hal_sdspi_xfer(uint8_t out);          /* shift one byte out and one in */
