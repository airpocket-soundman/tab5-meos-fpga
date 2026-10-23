#include <assert.h>
#include <string.h>
#include <stdio.h>
#define IO_UART_TEST 1
#include "../../drivers/io/uart.c"
static uint64_t now;
static bool alive=true,claimed,fail_start,rx_error,tx_idle;
static unsigned starts,stops,rx_count,tx_space,transmitted,sleeps;
static unsigned sleep_action;
static io_uart_handle_t replacement;
static task_t caller={.id=3,.state=TASK_RUNNING};
static bool caller_alive=true;
task_t *task_current(void){return &caller;}
int task_snapshot(task_info_t *out,int cap){assert(cap>0);if(!caller_alive)return 0;out[0].id=caller.id;out[0].state=caller.state;return 1;}
uint32_t arch_irq_disable(void){return 1;}
void arch_irq_restore(uint32_t flags){assert(flags==1);}
void mutex_lock(mutex_t *m){(void)m;}
void mutex_unlock(mutex_t *m){(void)m;}
bool io_owner_valid(io_owner_t o){return alive && (o==1 || o==2);}
int io_pins_claim(io_owner_t o,const int *p,size_t n,io_use_t use){
    assert(o==1 || o==2);assert(n==2 && use==IO_USE_UART);
    if(p[0]==37 || p[1]==38)return ERR_PERM;
    if(claimed)return ERR_BUSY;
    claimed=true;return 0;
}
int io_pins_release(io_owner_t o,const int *p,size_t n){(void)o;(void)p;assert(n==2);assert(claimed);assert(!current_handle);claimed=false;return 0;}
uint64_t hal_time_now_us(void){return now;}
void task_sleep(uint32_t ms){
    now+=(uint64_t)ms*1000;sleeps++;
    unsigned action=sleep_action;sleep_action=0;
    if(action==1){alive=false;io_uart_release_owner(1);}
    if(action==2){rx_count=2;}
    if(action==3){tx_space=2;}
    if(action==4){tx_idle=true;}
    if(action==5){
        io_uart_release_owner(1);
        io_uart_config_t c={1,6,7,115200};
        assert(!io_uart_open(2,&c,&replacement));
    }
    if(action==6){uint8_t x=0;assert(io_uart_read(1,current_handle,&x,1,0)==ERR_BUSY);}
}
static bool hw_start(const io_uart_config_t *c){assert(c->controller==1);starts++;return !fail_start;}
static bool hw_start_core(uint32_t baud,bool loopback){assert(baud==115200 && loopback);return false;}
static void hw_stop(void){stops++;}
static unsigned hw_rx(void *p,unsigned n){if(n>rx_count)n=rx_count;memset(p,'R',n);rx_count-=n;return n;}
static unsigned hw_tx(const void *p,unsigned n){assert(p || !n);if(n>tx_space)n=tx_space;tx_space-=n;transmitted+=n;return n;}
static bool hw_idle(void){return tx_idle;}
static bool hw_rx_error(void){bool e=rx_error;rx_error=false;return e;}
int main(void){
    io_uart_config_t c={1,6,7,115200};io_uart_handle_t h=99;char b[8]={0};
    c.controller=0;assert(io_uart_open(1,&c,&h)==ERR_NODEV && h==0);c.controller=1;
    c.tx_pin=7;assert(io_uart_open(1,&c,&h)==ERR_INVAL);c.tx_pin=6;
    c.baud=9599;assert(io_uart_open(1,&c,&h)==ERR_INVAL);c.baud=115200;
    c.tx_pin=37;assert(io_uart_open(1,&c,&h)==ERR_PERM && !starts);c.tx_pin=6;
    fail_start=true;assert(io_uart_open(1,&c,&h)==ERR_IO && !claimed);fail_start=false;
    assert(!io_uart_open(1,&c,&h) && claimed && h);
    assert(io_uart_read(1,h,0,0,0)==0);
    assert(io_uart_write(1,h,0,0,0)==0);
    io_uart_handle_t unused;assert(io_uart_open(2,&c,&unused)==ERR_BUSY);
    assert(io_uart_read(2,h,b,1,0)==ERR_PERM);
    assert(io_uart_read(1,h,b,1,0)==0 && !sleeps);
    rx_count=2;assert(io_uart_read(1,h,b,8,0)==2 && !memcmp(b,"RR",2));
    uint64_t before=now;assert(io_uart_read(1,h,b,8,3)==ERR_TIMEOUT && now-before==3000);
    rx_count=1;before=now;assert(io_uart_read(1,h,b,8,3)==1 && now-before==3000);
    sleep_action=2;assert(io_uart_read(1,h,b,2,3)==2);
    sleep_action=6;assert(io_uart_read(1,h,b,1,2)==ERR_TIMEOUT);
    rx_error=true;assert(io_uart_read(1,h,b,8,0)==ERR_IO);
    assert(io_uart_write(1,h,b,8,0)==0);
    tx_space=2;assert(io_uart_write(1,h,b,8,2)==2 && transmitted==2);
    sleep_action=3;assert(io_uart_write(1,h,b,2,3)==2 && transmitted==4);
    assert(io_uart_flush(1,h,0)==ERR_BUSY);
    sleep_action=4;assert(!io_uart_flush(1,h,3));
    sleep_action=1;assert(io_uart_read(1,h,b,1,3)==IO_ERR_CANCELLED && !claimed);
    alive=true;assert(io_uart_read(1,h,b,1,0)==ERR_BADF);
    io_uart_handle_t old=h;assert(!io_uart_open(1,&c,&h) && h!=old);
    sleep_action=5;assert(io_uart_read(1,h,b,1,3)==IO_ERR_CANCELLED && claimed);
    assert(io_uart_close(1,replacement)==ERR_PERM);
    assert(!io_uart_close(2,replacement) && !claimed);
    assert(io_uart_write(2,replacement,b,1,0)==ERR_BADF);
    assert(!io_uart_open(1,&c,&h));
    reading=99; /* Dead task left a pending read behind; recover this direction. */
    assert(io_uart_read(1,h,b,1,0)==0 && reading==0);
    writing=99;
    assert(io_uart_write(1,h,b,1,0)==0 && writing==0);
    assert(io_uart_read(1,h,0,1,0)==ERR_INVAL);
    assert(io_uart_write(1,h,b,65537,0)==ERR_INVAL);
    assert(io_uart_flush(1,h,60001)==ERR_INVAL);
    assert(!io_uart_close(1,h));
    assert(stops==4);
    puts("UART resource, deadline, partial IO, cancellation and stale-handle fixtures PASS");
}
