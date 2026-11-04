"""Compile the real P4 console against a CDC endpoint/host model."""
import argparse, subprocess, tempfile
from pathlib import Path
p = argparse.ArgumentParser()
p.add_argument('--cc', required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp/'hal').mkdir()
    (tmp/'hal/usb_serial_jtag_ll.h').write_text('''#include <stdint.h>
typedef struct { volatile uint32_t val; } ep_t;
extern struct usb_model { ep_t ep1; } USB_SERIAL_JTAG;
int usb_serial_jtag_ll_txfifo_writable(void);
void usb_serial_jtag_ll_write_txfifo(const uint8_t *,uint32_t);
void usb_serial_jtag_ll_txfifo_flush(void);
int usb_serial_jtag_ll_read_rxfifo(uint8_t *,uint32_t);
''')
    exe = tmp/'console.exe'
    subprocess.run([a.cc,'cc','-std=c11','-Wall','-Wextra','-Werror','-UNDEBUG',
        '-I'+str(tmp),'-I'+str(root/'kernel/include'),
        str(root/'hal/esp32p4/console_usbjtag.c'),
        str(root/'tests/usb_console_model.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
