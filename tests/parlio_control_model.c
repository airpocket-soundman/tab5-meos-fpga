#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <m5os/errno.h>
#include "hal/esp32p4/parlio_gpio_map.h"
enum {CLOCK_PIN=16,DIR_PIN=51,READY_PIN=45,SIG_GPIO_OUT_IDX=256};
static const int data_pins[]={18,19,5,3,4,2,48,47};
typedef struct {bool busy,failed;struct {const char*stage;}stats;} hal_parlio_t;
static hal_parlio_t ctx,*native_owner=&ctx;
static struct {
    struct {uint32_t val;}enable,enable1,in,in1;
    struct {unsigned out_sel;}func_out_sel_cfg[55];
} GPIO;
static unsigned ticks,pulses,writes,rx_at;static int levels[55];
static bool irq_enabled,tx_mode,warm_safe;
static uint8_t transmitted[8],receive[24];
static bool stream_safe_tx(hal_parlio_t*c){assert(c==&ctx);return warm_safe;}
static uint64_t hal_time_now_us(void){return ticks;}
static uint32_t arch_irq_disable(void){assert(irq_enabled);irq_enabled=false;return 1;}
static void arch_irq_restore(uint32_t f){assert(f==1&&!irq_enabled);irq_enabled=true;}
static void hal_delay_us(unsigned n){assert(!irq_enabled&&n==1);ticks++;}
static int hal_gpio_read(int pin){return levels[pin];}
static void present(uint8_t value){
    uint32_t low=~((3u<<18)|60u),high=~((1u<<16)|(1u<<15));
    for(unsigned i=0;i<8;i++)if(value&(1u<<i)){
        if(data_pins[i]<32)low|=1u<<data_pins[i];else high|=1u<<(data_pins[i]-32);
    }
    GPIO.in.val=low;GPIO.in1.val=high;
}
static void gpio_ll_set_level(void*hw,int pin,int value){
    assert(hw==&GPIO&&!irq_enabled);writes++;
    if(pin==CLOCK_PIN&&value&&!levels[pin]){
        if(tx_mode){
            assert(pulses<8);unsigned byte=0;
            for(unsigned i=0;i<8;i++)byte|=(unsigned)levels[data_pins[i]]<<i;
            transmitted[pulses]=(uint8_t)byte;
        }else if(++rx_at<24)present(receive[rx_at]);
        pulses++;
    }
    levels[pin]=value;
}
/* ACTUAL_CONTROL */
static void reset(bool tx){
    memset(&ctx,0,sizeof ctx);memset(&GPIO,0,sizeof GPIO);memset(levels,0,sizeof levels);
    ticks=pulses=writes=rx_at=0;irq_enabled=warm_safe=true;tx_mode=tx;
    GPIO.enable.val=1u<<CLOCK_PIN;
    for(unsigned i=0;i<55;i++)GPIO.func_out_sel_cfg[i].out_sel=SIG_GPIO_OUT_IDX;
    if(tx){GPIO.enable.val|=(3u<<18)|60u;GPIO.enable1.val=(1u<<16)|(1u<<15);}
    else {levels[DIR_PIN]=levels[READY_PIN]=1;present(receive[0]);}
}
int main(void){
    uint8_t q[8],out[24];size_t got;
    for(unsigned byte=0;byte<256;byte++){
        for(unsigned i=0;i<8;i++)q[i]=(uint8_t)(byte+i*31u);
        reset(true);assert(!hal_parlio_short_tx(&ctx,q,8,100));
        assert(irq_enabled&&ticks==24&&pulses==8&&!levels[CLOCK_PIN]);
        assert(!memcmp(q,transmitted,8));
        for(unsigned i=0;i<24;i++)receive[i]=(uint8_t)(byte+i*19u);
        reset(false);assert(!hal_parlio_short_rx(&ctx,out,24,&got,100));
        assert(irq_enabled&&ticks==72&&pulses==24&&got==24&&!levels[CLOCK_PIN]);
        assert(!memcmp(out,receive,24));
    }
    reset(true);GPIO.func_out_sel_cfg[data_pins[2]].out_sel=1;
    assert(hal_parlio_short_tx(&ctx,q,8,100)==ERR_BUSY&&!writes&&irq_enabled);
    reset(true);levels[DIR_PIN]=1;assert(hal_parlio_short_tx(&ctx,q,8,100)==ERR_BUSY&&!writes);
    reset(true);warm_safe=false;assert(hal_parlio_short_tx(&ctx,q,8,100)==ERR_BUSY&&!writes);
    reset(false);GPIO.enable.val|=1u<<18;
    assert(hal_parlio_short_rx(&ctx,out,24,&got,100)==ERR_BUSY&&!got&&!writes&&irq_enabled);
    reset(false);assert(hal_parlio_short_rx(&ctx,out,25,&got,100)==ERR_INVAL&&!writes);
    reset(false);assert(hal_parlio_short_rx(&ctx,out,24,&got,10)==ERR_TIMEOUT);
    assert(ctx.failed&&irq_enabled&&!levels[CLOCK_PIN]&&got<24&&ticks<=12);
    reset(true);assert(hal_parlio_short_tx(&ctx,q,8,10)==ERR_TIMEOUT);
    assert(ctx.failed&&irq_enabled&&!levels[CLOCK_PIN]&&pulses<8&&ticks<=12);
    puts("PASS actual GPIO control: all-byte TX/RX traces, bank packing, ownership/mux/direction guards, bounded IRQ loops, timeout cleanup");
}
