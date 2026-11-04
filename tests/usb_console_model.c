#include <m5os/hal_console.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "hal/usb_serial_jtag_ll.h"
struct usb_model USB_SERIAL_JTAG;
static int rx=-1;
/* Endpoint model: full packets remain buffered by the CDC host until a
 * later short packet/ZLP. A full endpoint is unavailable for two polls. */
static unsigned fill,pending,delivered,busy,polls,zlps;
static bool unplugged;
int usb_serial_jtag_ll_txfifo_writable(void) {
    /* Account for the preceding direct register TX write. */
    if(USB_SERIAL_JTAG.ep1.val){USB_SERIAL_JTAG.ep1.val=0;if(++fill==64){pending+=64;fill=0;busy=2;}}
    polls++;
    if(unplugged)return 0;
    if(busy){busy--;return 0;}
    return 1;
}
void usb_serial_jtag_ll_write_txfifo(const uint8_t *p,uint32_t n) {
    (void)p;assert(!unplugged&&!busy&&n==1);
    rx=-1; /* The stock RMW helper pops RX while writing TX. */
}
void usb_serial_jtag_ll_txfifo_flush(void) {
    if(USB_SERIAL_JTAG.ep1.val){USB_SERIAL_JTAG.ep1.val=0;if(++fill==64){pending+=64;fill=0;busy=2;}}
    if(unplugged||busy)return;
    if(!fill)zlps++;
    pending+=fill;fill=0;delivered+=pending;pending=0;
}
int usb_serial_jtag_ll_read_rxfifo(uint8_t *p,uint32_t n){assert(n==1);if(rx<0)return 0;*p=(uint8_t)rx;rx=-1;return 1;}
int main(void) {
    const unsigned sizes[]={1,31,63,64,65,128,318,320,448};
    for(unsigned i=0;i<sizeof sizes/sizeof sizes[0];i++){
        fill=pending=delivered=busy=polls=zlps=0;hal_console_init();
        for(unsigned n=0;n<sizes[i];n++)hal_console_putc('x');
        hal_console_flush();assert(delivered==sizes[i]&&!pending);
        if(sizes[i]%64==0)assert(zlps);
    }
    fill=pending=delivered=busy=0;hal_console_init();
    for(unsigned i=0;i<63;i++)hal_console_putc('x');
    hal_console_putc('\n');hal_console_flush();assert(delivered==64);
    unplugged=true;polls=0;hal_console_putc('x');hal_console_flush();
    assert(polls<=200001);unsigned before=polls;hal_console_putc('x');assert(polls==before+1);
    unplugged=false;hal_console_putc('x');hal_console_flush();assert(delivered==65);
    rx='Q';hal_console_putc('x');hal_console_flush();assert(hal_console_getc()=='Q');
    puts("PASS USB console full-packet ZLP, short/newline packets, unplug bound and recovery");
}
