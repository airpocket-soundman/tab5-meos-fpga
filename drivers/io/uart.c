#include <m5os/io_uart.h>
#include <m5os/mutex.h>
#include <m5os/hal_time.h>
#include <m5os/task.h>
#include <m5os/arch.h>
#include <m5os/hal_io_gpio.h>
#if defined(IO_UART_TEST)
#include "uart_fake_hw.h"
#elif defined(M5OS_BOARD_TAB5) && M5OS_BOARD_TAB5
#include "hal/uart_ll.h"
#include "soc/gpio_sig_map.h"
#include "soc/gpio_pins.h"
#include "esp_rom_gpio.h"
static int __DECLARE_RCC_ATOMIC_ENV;
static bool hw_sync(void) {
    UART1.reg_update.reg_update=1;
    uint64_t end=hal_time_now_us()+2000;
    unsigned spins=0;
    while(UART1.reg_update.reg_update) {
        if(++spins>=200000 || hal_time_now_us()>=end)return false;
    }
    return true;
}
static void hw_stop(void) {
    UART1.int_ena.val=0;
    esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ONE_INPUT,UART1_RXD_PAD_IN_IDX,false);
    uint32_t flags=arch_irq_disable();
    uart_ll_reset_register(UART_NUM_1);
    uart_ll_sclk_disable(&UART1);
    uart_ll_enable_bus_clock(UART_NUM_1,false);
    arch_irq_restore(flags);
}
static bool hw_start_core(uint32_t baud,bool loopback) {
    uint32_t flags=arch_irq_disable();
    uart_ll_enable_bus_clock(UART_NUM_1,true);
    uart_ll_reset_register(UART_NUM_1);
    uart_ll_set_sclk(&UART1,UART_SCLK_XTAL);
    uart_ll_sclk_enable(&UART1);
    uint32_t div=(40000000ULL+(uint64_t)UART_CLKDIV_V*baud-1)/((uint64_t)UART_CLKDIV_V*baud);
    uint32_t fixed=(40000000UL<<4)/(baud*div);
    HAL_FORCE_MODIFY_U32_REG_FIELD(HP_SYS_CLKRST.peri_clk_ctrl112,reg_uart1_sclk_div_num,div-1);
    UART1.clkdiv_sync.clkdiv=fixed>>4;
    UART1.clkdiv_sync.clkdiv_frag=fixed&15;
    UART1.conf0_sync.bit_num=UART_DATA_8_BITS;
    UART1.conf0_sync.stop_bit_num=UART_STOP_BITS_1;
    UART1.conf0_sync.parity_en=0;
    UART1.conf0_sync.tx_flow_en=0;
    UART1.conf0_sync.loopback=loopback;
    UART1.hwfc_conf_sync.rx_flow_en=0;
    UART1.int_ena.val=0;
    UART1.int_clr.val=UART_LL_INTR_MASK;
    arch_irq_restore(flags);
    if(!hw_sync())return false;
    UART1.conf0_sync.rxfifo_rst=1;
    UART1.conf0_sync.txfifo_rst=1;
    if(!hw_sync())return false;
    UART1.conf0_sync.rxfifo_rst=0;
    UART1.conf0_sync.txfifo_rst=0;
    if(!hw_sync())return false;
    return true;
}
static bool hw_start(const io_uart_config_t *c) {
    if(!hw_start_core(c->baud,false)){hw_stop();return false;}
    hal_io_gpio_input(c->rx_pin,0);
    hal_io_gpio_output(c->tx_pin,1);
    esp_rom_gpio_connect_in_signal(c->rx_pin,UART1_RXD_PAD_IN_IDX,false);
    esp_rom_gpio_connect_out_signal(c->tx_pin,UART1_TXD_PAD_OUT_IDX,false,false);
    return true;
}
static unsigned hw_rx(void *p,unsigned n) {
    unsigned count=uart_ll_get_rxfifo_len(&UART1);
    if(n>count)n=count;
    uart_ll_read_rxfifo(&UART1,p,n);
    return n;
}
static unsigned hw_tx(const void *p,unsigned n) {
    unsigned count=uart_ll_get_txfifo_len(&UART1);
    if(n>count)n=count;
    uart_ll_write_txfifo(&UART1,p,n);
    return n;
}
static bool hw_idle(void){return uart_ll_is_tx_idle(&UART1);}
static bool hw_rx_error(void) {
    uint32_t bits=UART1.int_raw.val&(UART_INTR_PARITY_ERR|UART_INTR_FRAM_ERR|UART_INTR_RXFIFO_OVF);
    if(bits)UART1.int_clr.val=bits;
    return bits!=0;
}
#else
static bool hw_start_core(uint32_t baud,bool loopback){(void)baud;(void)loopback;return false;}
static bool hw_start(const io_uart_config_t *c){(void)c;return false;}
static void hw_stop(void){}
static unsigned hw_rx(void *p,unsigned n){(void)p;(void)n;return 0;}
static unsigned hw_tx(const void *p,unsigned n){(void)p;(void)n;return 0;}
static bool hw_idle(void){return true;}
static bool hw_rx_error(void){return false;}
#endif

static mutex_t lock=MUTEX_INIT;
static io_owner_t current_owner;
static io_uart_handle_t current_handle,next_handle=1;
static int pins[2];
static uint32_t reading,writing;
int io_uart_selftest(void) {
    /* Contains zero GPIO pad/output-route calls; only the internal UART1 path.
     * No sleep: the bounded controller lock excludes normal opens throughout. */
    static const uint8_t pattern[]={0x00,0xff,0x55,0xaa,0x81,0x7e,0x12,0x34};
    mutex_lock(&lock);
    if(current_handle){mutex_unlock(&lock);return ERR_BUSY;}
    int result=ERR_IO;
    if(hw_start_core(115200,true)) {
        size_t sent=0,received=0;
        uint64_t end=hal_time_now_us()+20000;
        result=ERR_TIMEOUT;
        for(unsigned spins=0;spins<200000 && hal_time_now_us()<end;spins++) {
            if(hw_rx_error()){result=ERR_IO;break;}
            sent+=hw_tx(pattern+sent,(unsigned)(sizeof pattern-sent));
            uint8_t b[sizeof pattern];
            unsigned n=hw_rx(b,(unsigned)(sizeof pattern-received));
            bool mismatch=false;
            for(unsigned i=0;i<n;i++)
                if(b[i]!=pattern[received+i])mismatch=true;
            if(mismatch){result=ERR_IO;break;}
            received+=n;
            if(sent==sizeof pattern && received==sizeof pattern && hw_idle()){result=0;break;}
        }
    }
    hw_stop(); /* Reset clears loopback and FIFO; input is disconnected. */
    mutex_unlock(&lock);
    return result;
}
static bool task_alive(uint32_t pid) {
    if(pid==UINT32_MAX)return true; /* No scheduler/current task: do not steal. */
    task_info_t tasks[TASK_MAX];
    int n=task_snapshot(tasks,TASK_MAX);
    for(int i=0;i<n;i++)
        if(tasks[i].id==pid && tasks[i].state!=TASK_TERMINATED && tasks[i].state!=TASK_UNUSED)return true;
    return false;
}
static int valid(io_owner_t owner,io_uart_handle_t handle) {
    if(!handle || handle!=current_handle)return ERR_BADF;
    if(owner!=current_owner)return ERR_PERM;
    return io_owner_valid(owner)?0:IO_ERR_CANCELLED;
}
int io_uart_open(io_owner_t owner,const io_uart_config_t *c,io_uart_handle_t *out) {
    if(!c || !out)return ERR_INVAL;
    *out=0;
    if(c->controller!=1)return ERR_NODEV;
    if(c->baud<9600 || c->baud>1000000 || c->tx_pin<0 || c->rx_pin<0 ||
       c->tx_pin>=IO_PIN_COUNT || c->rx_pin>=IO_PIN_COUNT || c->tx_pin==c->rx_pin)return ERR_INVAL;
    mutex_lock(&lock);
    int result=0;
    if(current_handle)result=ERR_BUSY;
    else if(!next_handle)result=ERR_NOSPC;
    else if(!io_owner_valid(owner))result=ERR_PERM;
    else {
        int claim[2]={c->tx_pin,c->rx_pin};
        result=io_pins_claim(owner,claim,2,IO_USE_UART);
        if(!result) {
            if(!io_owner_valid(owner))result=IO_ERR_CANCELLED;
            else if(!hw_start(c))result=ERR_IO;
            if(result)io_pins_release(owner,claim,2);
            else {
                pins[0]=claim[0];pins[1]=claim[1];
                current_owner=owner;current_handle=next_handle++;
                reading=false;writing=false;*out=current_handle;
            }
        }
    }
    mutex_unlock(&lock);
    return result;
}
static void close_locked(void) {
    io_owner_t owner=current_owner;
    int release_pins[2]={pins[0],pins[1]};
    /* Publish invalidation before returning pins to the registry. A killed
     * closer must never leave a live handle pointing at reassigned GPIOs. */
    uint32_t flags=arch_irq_disable();
    current_handle=0;current_owner=0;reading=false;writing=false;
    hw_stop(); /* Register-only shutdown; no sync wait or registry mutex here. */
    arch_irq_restore(flags);
    io_pins_release(owner,release_pins,2);
}
int io_uart_close(io_owner_t owner,io_uart_handle_t handle) {
    mutex_lock(&lock);
    int result=valid(owner,handle);
    if(!result)close_locked();
    mutex_unlock(&lock);
    return result;
}
void io_uart_release_owner(io_owner_t owner) {
    mutex_lock(&lock);
    if(current_handle && current_owner==owner)close_locked();
    mutex_unlock(&lock);
}
static int transfer(io_owner_t owner,io_uart_handle_t handle,void *buf,size_t len,uint32_t timeout,int mode) {
    if(timeout>60000 || len>65536 || (len && !buf))return ERR_INVAL;
    uint64_t end=hal_time_now_us()+(uint64_t)timeout*1000;
    mutex_lock(&lock);
    int result=valid(owner,handle);
    uint32_t *busy=mode==0?&reading:&writing;
    if(!result && *busy && task_alive(*busy))result=ERR_BUSY;
    if(result){mutex_unlock(&lock);return result;}
    task_t *task=task_current();
    *busy=task?task->id:UINT32_MAX;
    size_t done=0;
    for(;;) {
        result=valid(owner,handle);
        if(result){result=IO_ERR_CANCELLED;break;}
        if(mode!=2 && !len){result=0;break;}
        if(mode==0 && hw_rx_error()){result=ERR_IO;break;}
        if(mode==2){if(hw_idle()){result=0;break;}}
        else {
            unsigned n=mode==0?hw_rx((uint8_t*)buf+done,(unsigned)(len-done)):
                hw_tx((const uint8_t*)buf+done,(unsigned)(len-done));
            done+=n;
            if(done==len){result=(int)done;break;}
        }
        if(!timeout){result=mode==2?ERR_BUSY:(int)done;break;}
        if(hal_time_now_us()>=end){result=ERR_TIMEOUT;break;}
        mutex_unlock(&lock);
        task_sleep(1);
        mutex_lock(&lock);
    }
    if(current_handle==handle && current_owner==owner)*busy=false;
    mutex_unlock(&lock);
    return done?(int)done:result;
}
int io_uart_read(io_owner_t o,io_uart_handle_t h,void *p,size_t n,uint32_t t){return transfer(o,h,p,n,t,0);}
int io_uart_write(io_owner_t o,io_uart_handle_t h,const void *p,size_t n,uint32_t t){return transfer(o,h,(void*)p,n,t,1);}
int io_uart_flush(io_owner_t o,io_uart_handle_t h,uint32_t t){return transfer(o,h,0,0,t,2);}
