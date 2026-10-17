#pragma once
#include <m5os/types.h>
/* /dev/uart - the console (USB-Serial-JTAG on CoreS3).
 *   write(): bytes to the host ('\n' is sent as CR LF), serialized with kprintf.
 *   read():  bytes received so far.  By default it returns at once (0 if none).
 *            UART_IOC_SET_RX_TIMEOUT changes that for the whole device:
 *            0 = non-blocking, >0 = wait up to that many ms for at least one byte,
 *            -1 = wait forever.  A waiting reader does not block writers. */
#define UART_IOC_SET_RX_TIMEOUT   0x0301     /* arg: int32_t* */
