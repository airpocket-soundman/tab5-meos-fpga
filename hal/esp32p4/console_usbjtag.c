/*
 * Console over the ESP32-P4's USB-Serial-JTAG peripheral (the USB-C port that the host sees as
 * /dev/cu.usbmodem*).  Same approach as the ESP32-S3 version: only hal/usb_serial_jtag_ll.h
 * (static-inline register access, verified free of FreeRTOS); the peripheral was enabled by the
 * ROM/bootloader, so "init" configures nothing.
 */
#include <m5os/hal_console.h>
#include "hal/usb_serial_jtag_ll.h"

/* If no host is reading, the TX FIFO never drains: give up after this many polls so the kernel keeps
 * running with the cable unplugged. */
#define CONSOLE_TX_SPIN_LIMIT  200000u

static bool s_host_stalled;
static unsigned s_packet_bytes;
static bool s_need_zlp;

void hal_console_init(void) { s_host_stalled = false; s_packet_bytes = 0; s_need_zlp = false; }

void hal_console_putc(char c)
{
    if (s_host_stalled) {
        if (!usb_serial_jtag_ll_txfifo_writable()) return;
        s_host_stalled = false;
    }
    uint32_t spins = CONSOLE_TX_SPIN_LIMIT;
    while (!usb_serial_jtag_ll_txfifo_writable()) {
        if (--spins == 0) { s_host_stalled = true; return; }
    }
    uint8_t b = (uint8_t)c;
    /* EP1 reads pop RX, writes push TX. The IDF LL helper performs a
     * read/modify/write, silently consuming a concurrent input byte.
     * RDWR_BYTE occupies bits0..7; reserved bits are written as zero. */
    USB_SERIAL_JTAG.ep1.val = (uint32_t)b;
    s_need_zlp = ++s_packet_bytes == 64;
    if (s_need_zlp) s_packet_bytes = 0;
    if (c == '\n') { usb_serial_jtag_ll_txfifo_flush(); s_packet_bytes = 0; }         /* packets only leave the chip when flushed */
}

void hal_console_flush(void)
{
    if (s_host_stalled) return;
    usb_serial_jtag_ll_txfifo_flush();
    /* P4 LL/stock IDF driver: a full 64-byte endpoint packet needs a second
     * flush AFTER the host takes it, to send the terminating zero-length
     * packet. Otherwise Windows retains a complete diagnostic until more
     * data arrives, making a finished render appear hung. Keep unplugged
     * and panic paths bounded; no scheduler/FreeRTOS dependency here. */
    if (s_need_zlp) {
        uint32_t spins = CONSOLE_TX_SPIN_LIMIT;
        while (!usb_serial_jtag_ll_txfifo_writable()) {
            if (--spins == 0) { s_host_stalled = true; return; }
        }
        usb_serial_jtag_ll_txfifo_flush();
        s_need_zlp = false;
    }
    s_packet_bytes = 0;
}

int hal_console_getc(void)
{
    uint8_t b;
    if (usb_serial_jtag_ll_read_rxfifo(&b, 1) == 1) return b;
    return -1;
}
