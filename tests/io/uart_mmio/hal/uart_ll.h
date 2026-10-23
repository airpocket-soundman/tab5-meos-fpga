#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define UART_NUM_1 1
#define UART_SCLK_XTAL 0
#define UART_DATA_8_BITS 3
#define UART_STOP_BITS_1 1
#define UART_CLKDIV_V 4095
#define UART_LL_INTR_MASK 0x7ffff
#define UART_INTR_PARITY_ERR 4
#define UART_INTR_FRAM_ERR 8
#define UART_INTR_RXFIFO_OVF 16
#define HAL_FORCE_MODIFY_U32_REG_FIELD(reg,field,x) ((reg).field=(x))
typedef struct {
 struct {unsigned reg_update;} reg_update;
 struct {unsigned val;} int_ena,int_clr,int_raw;
 struct {unsigned clkdiv,clkdiv_frag;} clkdiv_sync;
 struct {unsigned bit_num,stop_bit_num,parity_en,tx_flow_en,loopback,rxfifo_rst,txfifo_rst;} conf0_sync;
 struct {unsigned rx_flow_en;} hwfc_conf_sync;
} uart_fake_regs;
static uart_fake_regs UART1;
static struct {struct {unsigned reg_uart1_sclk_div_num;} peri_clk_ctrl112;} HP_SYS_CLKRST;
static bool bus,clock_on;
static unsigned resets,output_routes,pad_touches;
static uint8_t fifo[128];static unsigned fifo_n;
static bool corrupt,stall,sync_stall;
static void fake_reset(void){UART1=(uart_fake_regs){0};fifo_n=0;resets++;}
#define uart_ll_enable_bus_clock(port,en) ((void)(port),(void)__DECLARE_RCC_ATOMIC_ENV,bus=(en))
#define uart_ll_reset_register(port) ((void)(port),(void)__DECLARE_RCC_ATOMIC_ENV,fake_reset())
#define uart_ll_set_sclk(hw,src) ((void)(hw),(void)(src),(void)__DECLARE_RCC_ATOMIC_ENV)
#define uart_ll_sclk_enable(hw) ((void)(hw),(void)__DECLARE_RCC_ATOMIC_ENV,clock_on=true)
#define uart_ll_sclk_disable(hw) ((void)(hw),(void)__DECLARE_RCC_ATOMIC_ENV,clock_on=false)
static unsigned uart_ll_get_rxfifo_len(uart_fake_regs *h){(void)h;return fifo_n;}
static unsigned uart_ll_get_txfifo_len(uart_fake_regs *h){(void)h;return stall?0:128-fifo_n;}
static void uart_ll_write_txfifo(uart_fake_regs *h,const uint8_t *p,unsigned n){assert(bus && clock_on);if(h->conf0_sync.loopback)for(unsigned i=0;i<n;i++)fifo[fifo_n++]=p[i]^(corrupt?1:0);}
static void uart_ll_read_rxfifo(uart_fake_regs *h,uint8_t *p,unsigned n){(void)h;for(unsigned i=0;i<n;i++)p[i]=fifo[i];for(unsigned i=n;i<fifo_n;i++)fifo[i-n]=fifo[i];fifo_n-=n;}
static bool uart_ll_is_tx_idle(uart_fake_regs *h){(void)h;return true;}
