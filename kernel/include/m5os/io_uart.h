#pragma once
#include <m5os/io.h>
/* Task context only. UART1, polling, 8N1; no hardware/software flow control. */
typedef uint32_t io_uart_handle_t;
typedef struct { unsigned controller; int tx_pin,rx_pin; uint32_t baud; } io_uart_config_t;
/* Handles never reused. Only one UART1 object can be open at a time. */
int io_uart_open(io_owner_t owner,const io_uart_config_t *config,io_uart_handle_t *out);
/* timeout_ms <= 60000. Zero: nonblocking, returns zero when no progress.
 * Positive: total deadline, ERR_TIMEOUT if no progress; partial bytecount wins.
 * Write counts FIFO acceptance, not wire completion. Maximum length 65536. */
int io_uart_read(io_owner_t owner,io_uart_handle_t handle,void *data,size_t len,uint32_t timeout_ms);
int io_uart_write(io_owner_t owner,io_uart_handle_t handle,const void *data,size_t len,uint32_t timeout_ms);
int io_uart_flush(io_owner_t owner,io_uart_handle_t handle,uint32_t timeout_ms);
/* Close cancels pending operations and discards queued TX/RX. Flush first if needed. */
int io_uart_close(io_owner_t owner,io_uart_handle_t handle);
void io_uart_release_owner(io_owner_t owner);
/* Internal UART1 TX/RX loopback only: no external GPIO configuration or drive.
 * Requires controller idle; returns zero on PASS, negative error otherwise. */
int io_uart_selftest(void);
