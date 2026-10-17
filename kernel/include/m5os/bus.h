#pragma once
/* The CoreS3 LCD and the SD card share SCLK/MOSI (and GPIO35 = MISO = LCD D/C).
 * Every transaction on either must hold this lock.  It degrades to a no-op
 * before the scheduler runs or when interrupts are masked (it cannot block
 * there), which is fine: nothing else runs concurrently in those situations. */
void spi_bus_lock(void);
void spi_bus_unlock(void);
