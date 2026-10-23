#include <assert.h>
#include <stdio.h>
#define M5OS_BOARD_TAB5 1
#include "../../drivers/io/uart.c"
static uint64_t now;
uint64_t hal_time_now_us(void){now++;if(!sync_stall)UART1.reg_update.reg_update=0;return now;}
uint32_t arch_irq_disable(void){return 1;}
void arch_irq_restore(uint32_t f){assert(f==1);}
void mutex_lock(mutex_t *m){(void)m;}
void mutex_unlock(mutex_t *m){(void)m;}
task_t *task_current(void){return 0;}
int task_snapshot(task_info_t *p,int n){(void)p;(void)n;return 0;}
void task_sleep(uint32_t ms){(void)ms;assert(0);}
bool io_owner_valid(io_owner_t o){(void)o;return true;}
int io_pins_claim(io_owner_t o,const int *p,size_t n,io_use_t u){(void)o;(void)p;(void)n;(void)u;assert(0);return ERR_IO;}
int io_pins_release(io_owner_t o,const int *p,size_t n){(void)o;(void)p;(void)n;assert(0);return ERR_IO;}
void hal_io_gpio_input(int p,int pull){(void)p;(void)pull;pad_touches++;}
void hal_io_gpio_output(int p,int init){(void)p;(void)init;pad_touches++;}
int main(void){
    assert(io_uart_selftest()==0);
    assert(!bus && !clock_on && !UART1.conf0_sync.loopback && !fifo_n);
    assert(!output_routes && !pad_touches);
    unsigned previous=resets;current_handle=8;
    assert(io_uart_selftest()==ERR_BUSY && resets==previous);current_handle=0;
    corrupt=true;assert(io_uart_selftest()==ERR_IO);corrupt=false;
    assert(!bus && !clock_on && !UART1.conf0_sync.loopback);
    stall=true;uint64_t before=now;assert(io_uart_selftest()==ERR_TIMEOUT);stall=false;
    assert(now-before<=20100 && !bus && !clock_on);
    sync_stall=true;before=now;assert(io_uart_selftest()==ERR_IO);sync_stall=false;
    assert(now-before<=2100 && !bus && !clock_on);
    assert(io_uart_selftest()==0 && !output_routes && !pad_touches);
    puts("UART actual register path internal loopback, no GPIO routes, fault cleanup PASS");
}
