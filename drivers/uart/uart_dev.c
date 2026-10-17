#include <m5os/device.h>
#include <m5os/app.h>
#include <m5os/errno.h>
#include <m5os/hal_console.h>
#include <m5os/hal_time.h>
#include <m5os/task.h>
#include <m5os/kprintf.h>
#include <m5os/dev_uart.h>

#define RX_POLL_MS   2u              /* effective period is one scheduler tick */

static volatile int32_t s_rx_timeout_ms;      /* 0 non-blocking, -1 forever */

static int drain(uint8_t *p, size_t len)
{
    size_t n = 0;
    while (n < len) {
        int c = kconsole_getc();
        if (c < 0) break;
        p[n++] = (uint8_t)c;
    }
    return (int)n;
}

/* Polls the receive FIFO, sleeping between polls.  (An RX interrupt that wakes a
 * BLOCKED reader would be the next step; polling keeps the driver trivial.) */
static int uart_read(device_t *dev, void *buf, size_t len)
{
    (void)dev;
    if (len == 0) return 0;
    int n = drain(buf, len);
    if (n > 0 || s_rx_timeout_ms == 0) return n;

    uint64_t deadline = hal_time_now_us() + (uint64_t)(s_rx_timeout_ms > 0 ? s_rx_timeout_ms : 0) * 1000u;
    for (;;) {
        task_sleep(RX_POLL_MS);
        n = drain(buf, len);
        if (n > 0) return n;
        if (s_rx_timeout_ms == 0) return 0;
        if (s_rx_timeout_ms > 0 && hal_time_now_us() >= deadline) return 0;
    }
}

static int uart_write(device_t *dev, const void *buf, size_t len)
{
    (void)dev;
    kconsole_write(buf, len);
    return (int)len;
}

static int uart_ioctl(device_t *dev, uint32_t cmd, void *arg)
{
    (void)dev;
    if (cmd != UART_IOC_SET_RX_TIMEOUT || !arg) return ERR_INVAL;
    s_rx_timeout_ms = *(int32_t *)arg;
    return ERR_OK;
}

static const device_ops_t uart_ops = { .read = uart_read, .write = uart_write, .ioctl = uart_ioctl };
static device_t uart_device = { .name = "uart", .ops = &uart_ops, .flags = DEV_F_UNLOCKED_READ, .required_perm = APP_PERM_CONSOLE };

void uart_dev_register(void) { device_register(&uart_device); }
