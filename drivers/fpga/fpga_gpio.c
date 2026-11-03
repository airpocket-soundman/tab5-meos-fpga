#include "fpga_gpio.h"
#include <m5os/hal_gpio.h>
#include <m5os/hal_time.h>
#include <string.h>

/* Carrier e5a386d, Dock jumper table. AUX stays input but is reserved too. */
static const int pins[] = {18,19,5,3,4,2,48,47,16,51,45,17,52,6,7};
enum { CLK=16, DIR=51, READY=45, PIN_COUNT=15 };
static bool cancelled(void *ctx)
{
    fpga_gpio_t *g=ctx;
    return g->cancel && g->cancel(g->cancel_context);
}
static int check(fpga_gpio_t *g, uint64_t end)
{
    if(cancelled(g)) g->error=FPGA_LINK_CANCELLED;
    else if(hal_time_now_us()>=end) g->error=FPGA_LINK_TIMEOUT;
    return g->error;
}
static int config(fpga_gpio_t *g,int p,bool output,int value)
{
    if(io_gpio_config(g->owner,p,output,value,IO_PULL_NONE)) {
        g->error=FPGA_LINK_IO; return -1;
    }
    return 0;
}
static int write_pin(fpga_gpio_t *g,int p,int value)
{
    if(io_gpio_write(g->owner,p,value)) {g->error=FPGA_LINK_IO;return -1;}
    return 0;
}
static int data_input(fpga_gpio_t *g)
{
    int error=0;
    for(unsigned i=0;i<8;i++) if(config(g,pins[i],false,0)) error=-1;
    return error;
}
static void safe(fpga_gpio_t *g)
{
    if(!g->claimed) return;
    int original=g->error;
    data_input(g);
    /* Set LOW before disabling the control outputs. Input pins may reject
     * writes during partial initialization; still try every cleanup step. */
    io_gpio_write(g->owner,CLK,0); io_gpio_write(g->owner,DIR,0);
    for(unsigned i=8;i<PIN_COUNT;i++) config(g,pins[i],false,0);
    if(original) g->error=original;
}
void fpga_gpio_close(fpga_gpio_t *g)
{
    if(!g) return;
    safe(g);
    if(g->owner) io_owner_close(g->owner);
    g->owner=0;g->claimed=false;
}
int fpga_gpio_open(fpga_gpio_t *g,const char *profile)
{
    if(!g || !profile || strcmp(profile,"dock-jumper-v0")) return FPGA_LINK_INVALID;
    if(!M5OS_FPGA_DOCK_VERIFIED) return FPGA_LINK_INVALID;
    if(g->owner) return FPGA_LINK_BUSY;
    memset(g,0,sizeof *g);
    if(io_owner_create("fpga-dock",&g->owner)) return FPGA_LINK_BUSY;
    if(io_pins_claim(g->owner,pins,PIN_COUNT,IO_USE_DIGITAL)) {
        fpga_gpio_close(g);return FPGA_LINK_BUSY;
    }
    g->claimed=true;
    for(unsigned i=0;i<PIN_COUNT;i++) {
        if(config(g,pins[i],false,0)) {fpga_gpio_close(g);return FPGA_LINK_IO;}
    }
    return 0;
}
static int pause_us(fpga_gpio_t *g,unsigned us,uint64_t end)
{
    if(check(g,end)) return -1;
    hal_delay_us(us);
    return check(g,end)?-1:0;
}
static int wait_ready(fpga_gpio_t *g,int expected,uint64_t end)
{
    /* Also bounded if the monotonic timer stops. */
    for(unsigned i=0;i<1000;i++) {
        int value;
        if(check(g,end)) return -1;
        if(io_gpio_read(g->owner,READY,&value)) {g->error=FPGA_LINK_IO;return -1;}
        if(value==expected) return 0;
        if(pause_us(g,100,end)) return -1;
    }
    g->error=FPGA_LINK_TIMEOUT;return -1;
}
static int begin(void *ctx,uint64_t end)
{
    fpga_gpio_t *g=ctx;
    if(!g->claimed || g->failed) {g->error=FPGA_LINK_IO;return -1;}
    g->error=0;
    g->sent=0;g->received=0;g->elapsed_us=0;
    memset(g->request,0,sizeof g->request);memset(g->response,0,sizeof g->response);
    g->started_us=hal_time_now_us();
    if(check(g,end) || data_input(g) || config(g,CLK,true,0) || config(g,DIR,true,0)
       || wait_ready(g,0,end) || pause_us(g,200,end)) return -1;
    for(unsigned i=0;i<8;i++) if(config(g,pins[i],true,0)) return -1;
    return 0;
}
static int pulse(fpga_gpio_t *g,uint64_t end)
{
    if(check(g,end) || write_pin(g,CLK,1) || pause_us(g,100,end)
       || write_pin(g,CLK,0) || pause_us(g,100,end)) return -1;
    return 0;
}
static int send(void *ctx,const uint8_t *data,size_t n,uint64_t end)
{
    fpga_gpio_t *g=ctx;
    for(size_t j=0;j<n;j++) {
        if(check(g,end)) return -1;
        for(unsigned i=0;i<8;i++) if(write_pin(g,pins[i],(data[j]>>i)&1)) return -1;
        if(pause_us(g,100,end) || pulse(g,end)) return -1;
        if(g->sent<8)g->request[g->sent++]=data[j];
    }
    return (int)n;
}
static int turnaround(void *ctx,uint64_t end)
{
    fpga_gpio_t *g=ctx;
    return data_input(g) || pause_us(g,200,end) || write_pin(g,DIR,1)
        || pause_us(g,200,end) || wait_ready(g,1,end) ? -1 : 0;
}
static int receive(void *ctx,uint8_t *data,size_t n,uint64_t end)
{
    fpga_gpio_t *g=ctx;
    for(size_t j=0;j<n;j++) {
        int ready;
        if(check(g,end)) return -1;
        if(io_gpio_read(g->owner,READY,&ready) || !ready) {g->error=FPGA_LINK_IO;return -1;}
        data[j]=0;
        for(unsigned i=0;i<8;i++) {
            int value;
            if(io_gpio_read(g->owner,pins[i],&value)) {g->error=FPGA_LINK_IO;return -1;}
            data[j]|=(uint8_t)(value<<i);
        }
        if(g->received<8)g->response[g->received++]=data[j];
        if(pulse(g,end)) return -1;
    }
    if(write_pin(g,DIR,0) || pause_us(g,200,end) || wait_ready(g,0,end)) return -1;
    return (int)n;
}
static void finish(void *ctx,bool failed)
{
    fpga_gpio_t *g=ctx;
    g->failed=failed;
    g->elapsed_us=hal_time_now_us()-g->started_us;
    safe(g);
}
static uint64_t now(void *ctx) {(void)ctx;return hal_time_now_us();}
static void idle(void *ctx) {(void)ctx;hal_delay_us(100);}
const fpga_link_ops_t fpga_gpio_ops={begin,send,turnaround,receive,finish,now,idle,cancelled};
