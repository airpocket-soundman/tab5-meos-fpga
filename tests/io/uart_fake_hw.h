#pragma once
static bool hw_start(const io_uart_config_t *c);
static bool hw_start_core(uint32_t baud,bool loopback);
static void hw_stop(void);
static unsigned hw_rx(void *p,unsigned n);
static unsigned hw_tx(const void *p,unsigned n);
static bool hw_idle(void);
static bool hw_rx_error(void);
