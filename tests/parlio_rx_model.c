/* Polling RX sequence model. Actual register/header compilation is separate. */
#include <m5os/hal_parlio.h>
#include <m5os/errno.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#ifdef NDEBUG
#error Assertions required
#endif
enum { CLOCK_PIN=16,DIR_PIN=51,READY_PIN=45,DMA_CAPACITY=2688,POLL_LIMIT=1000,
 GDMA_LL_RX_EVENT_MASK=0x7ff, PARLIO_LL_EVENT_RX_MASK=2,PARLIO_LL_EVENT_RX_FIFO_FULL=2,
 SIG_GPIO_OUT_IDX=256,PARLIO_CLK_SRC_XTAL=1,PARLIO_CLK_SRC_PLL_F160M=2,
 PARLIO_BIT_PACK_ORDER_LSB=0,PARLIO_LL_RX_EOF_COND_RX_FULL=0,
 DMA_DESCRIPTOR_BUFFER_OWNER_DMA=1,GDMA_TRIG_PERIPH_PARLIO=3 };
static const int data_pins[8]={18,19,5,3,4,2,48,47};
typedef struct desc {struct {unsigned size,length,owner,suc_eof,err_eof;} dw0;void *buffer;struct desc *next;} dma_descriptor_align8_t;
struct hal_parlio {uint32_t hz,rx_hz;int rx_channel;uint8_t *payload;dma_descriptor_align8_t *desc;bool rx_started,dma_quarantined;hal_parlio_stats_t stats;};
static struct {struct {unsigned rx_clk_i_inv,rx_clk_o_inv;} rx_clk_cfg;struct {unsigned rx_reg_update;} reg_update;struct {uint32_t val;} int_raw;} PARL_IO;
static struct {struct {unsigned out_inv_sel;}func_out_sel_cfg[55];}GPIO;
static struct {int unused;} AXI_DMA;
static const struct {struct {struct {int data_sigs[16],clk_out_sig;}rx_units[1];}groups[1];}parlio_periph_signals={.groups={{.rx_units={{.data_sigs={1,2,3,4,5,6,7,8,0,0,0,0,0,0,0,16},.clk_out_sig=99}}}}};
typedef struct {unsigned integer,numerator,denominator;}hal_utils_clk_div_t;
enum {NORMAL,NO_EOF,REG_STALL,DMA_ERROR,SHORT,LONG_COUNT,BAD_OWNER,BAD_EOF,BAD_ADDR,PHASE_DRIFT,CACHE_FAIL,BAD_ERR_EOF,CACHE_PAYLOAD_FAIL,PREEMPTION,GATE_FAIL,GATE_FAIL_LATE,GATE_ROUTE_FAIL,STOP_FAIL};
static int fault,irq_depth,level[55],output[55],clock_signal;static unsigned now_us,rx_bitlen,starts,cache_reads,resets,reset_wait_calls;static bool rx_clock,rx_start,soft,descriptor_ready,stopped,gate_enabled,gate_armed,event_seen,task_seen;static uint32_t dma_raw;
static hal_parlio_t *active;
static uint64_t hal_time_now_us(void){return now_us;}
static int check(uint64_t deadline,hal_parlio_cancel_t cancel,void *arg){now_us++;if(fault!=REG_STALL)PARL_IO.reg_update.rx_reg_update=0;if(cancel&&cancel(arg))return HAL_PARLIO_CANCELLED;return now_us>=deadline?ERR_TIMEOUT:0;}
static int sync_c2m(void *p,size_t n){(void)p;(void)n;assert(!active->rx_started);return 0;}
static int sync_m2c(void *p,size_t n){(void)p;(void)n;assert(stopped&&!active->rx_started);cache_reads++;return fault==CACHE_FAIL||(fault==CACHE_PAYLOAD_FAIL&&cache_reads==2)?ERR_IO:0;}
static void restore_bus(bool out){for(unsigned i=0;i<8;i++)output[data_pins[i]]=0;clock_signal=SIG_GPIO_OUT_IDX;level[CLOCK_PIN]=0;output[CLOCK_PIN]=out;}
static int stop_dma(hal_parlio_t*c,bool rx){assert(rx&&!rx_clock&&!rx_start&&!soft);if(fault==STOP_FAIL&&c->rx_started){c->dma_quarantined=true;c->stats.stage="dma_quarantined";return ERR_TIMEOUT;}c->rx_started=false;stopped=true;return 0;}
static uint32_t arch_irq_disable(void){return irq_depth++;}
static void arch_irq_restore(uint32_t prev){assert(irq_depth==((int)prev+1));irq_depth=(int)prev;}
static void _parlio_ll_rx_enable_clock(void*p,bool en){(void)p;assert(irq_depth);rx_clock=en;}
static void _parlio_ll_rx_set_clock_source(void*p,int src){(void)p;assert(irq_depth);assert(src==(active->rx_hz==500000?PARLIO_CLK_SRC_XTAL:PARLIO_CLK_SRC_PLL_F160M));}
static void _parlio_ll_rx_set_clock_div(void*p,const hal_utils_clk_div_t*d){(void)p;assert(irq_depth&&d->integer>0&&d->integer<=256);assert(d->integer==(active->rx_hz==500000?40000000u:160000000u)/active->rx_hz);assert(d->numerator==0&&d->denominator==0);}
static void _parlio_ll_rx_reset_clock(void*p){(void)p;assert(irq_depth&&rx_clock&&clock_signal==SIG_GPIO_OUT_IDX);resets++;}
static void hal_delay_us(unsigned us){assert(rx_clock&&clock_signal==SIG_GPIO_OUT_IDX);assert(us>=(2000000u+active->rx_hz-1u)/active->rx_hz+2u);reset_wait_calls++;now_us+=us;}
static int hal_gpio_read(int pin){return level[pin];}
static void hal_gpio_write(int pin,int value)
{
    assert(pin==17&&value==1&&output[pin]&&gate_armed&&gate_enabled);
    assert(!irq_depth&&rx_clock&&rx_start&&!soft&&active->rx_started&&clock_signal==99);
    level[pin]=value;
    if(fault==NO_EOF)return;
    if(fault==DMA_ERROR){dma_raw=8;return;}
    unsigned n=rx_bitlen/8;
    active->desc->dw0.length=fault==SHORT?n-4:fault==LONG_COUNT?n+4:n;
    active->desc->dw0.owner=fault==BAD_OWNER;
    active->desc->dw0.suc_eof=fault!=BAD_EOF;
    active->desc->dw0.err_eof=fault==BAD_ERR_EOF;
    for(unsigned i=0;i<n;i++)active->payload[i]=(uint8_t)(i^0xc3);
    dma_raw=3;event_seen=true;
    if(fault!=GATE_FAIL&&fault!=GATE_FAIL_LATE){level[17]=0;task_seen=true;}
    if(fault==PHASE_DRIFT)PARL_IO.rx_clk_cfg.rx_clk_i_inv=1;
    /* Simulate 3ms of CPU preemption after finite EOF. Only the modeled
     * hardware task can stop clocks during that interval, not a CPU poll. */
    if(fault==PREEMPTION||fault==GATE_FAIL_LATE){now_us+=3000;if(level[17])PARL_IO.int_raw.val=2;}
}
static int rx_gate_arm(hal_parlio_t*c)
{
    c->stats.rx_gate_verified=c->stats.rx_gate_closed=false;
    c->stats.gate_event_seen=c->stats.gate_task_seen=false;
    assert(level[17]==0&&output[17]);
    if(fault==GATE_ROUTE_FAIL)return ERR_IO;
    gate_armed=true;return 0;
}
static bool rx_gate_ready(hal_parlio_t*c){return c->stats.rx_gate_verified=gate_armed&&output[17]&&!level[17];}
static bool rx_gate_closed(hal_parlio_t*c)
{
    c->stats.gate_event_seen=event_seen;c->stats.gate_task_seen=task_seen;
    return c->stats.rx_gate_closed=gate_armed&&event_seen&&task_seen&&!level[17];
}
static void rx_gate_disarm(hal_parlio_t*c){(void)c;gate_armed=false;level[17]=0;output[17]=0;}

static void hal_io_gpio_input(int pin,int pull){assert(pull==0);output[pin]=0;}
static void gpio_ll_set_output_enable_ctrl(void*p,int pin,bool periph,bool inv){(void)p;(void)pin;assert(!periph&&!inv);}
static void gpio_ll_set_level(void*p,int pin,int value){(void)p;level[pin]=value;}
static void gpio_ll_output_enable(void*p,int pin){(void)p;assert(pin==CLOCK_PIN);output[pin]=1;}
static void gpio_ll_input_enable(void*p,int pin){(void)p;assert(pin==17&&output[pin]);}
static void esp_rom_gpio_connect_in_signal(int pin,int signal,bool invert){assert(!invert);if(pin==17)assert(signal==16&&output[pin]);else assert(!output[pin]&&signal>=1&&signal<=8);}
static void esp_rom_gpio_connect_out_signal(int pin,int signal,bool invert,bool oe){assert(pin==CLOCK_PIN&&!oe);if(signal!=SIG_GPIO_OUT_IDX){assert(!rx_clock&&level[pin]==1&&rx_start&&!soft&&gate_enabled&&invert&&signal==99);}clock_signal=signal;GPIO.func_out_sel_cfg[pin].out_inv_sel=invert;}
static void axi_dma_ll_rx_enable_interrupt(void*p,int ch,unsigned mask,bool en){(void)p;assert(ch==2&&mask==GDMA_LL_RX_EVENT_MASK&&!en);}
static void axi_dma_ll_rx_clear_interrupt_status(void*p,int ch,unsigned mask){(void)p;assert(ch==2&&mask==GDMA_LL_RX_EVENT_MASK);}
static void axi_dma_ll_rx_reset_channel(void*p,int ch){(void)p;assert(ch==2&&!active->rx_started);}
static void axi_dma_ll_rx_abort(void*p,int ch,bool abort){(void)p;assert(ch==2&&!abort);}
static void parlio_ll_rx_reset_fifo(void*p){(void)p;assert(rx_clock&&clock_signal==SIG_GPIO_OUT_IDX);}
static void parlio_ll_rx_start(void*p,bool en){(void)p;assert(!rx_clock);rx_start=en;}
static void parlio_ll_rx_start_soft_recv(void*p,bool en){(void)p;assert(!rx_clock&&!en);soft=en;}
static void parlio_ll_rx_set_bus_width(void*p,unsigned width){(void)p;assert(width==8);}
static void parlio_ll_rx_set_bit_pack_order(void*p,unsigned order){(void)p;assert(order==0);}
static void parlio_ll_rx_set_level_recv_mode(void*p,bool active_low){(void)p;assert(!active_low);}
static void parlio_ll_rx_treat_data_line_as_en(void*p,unsigned line){(void)p;assert(line==15);}
static void parlio_ll_rx_set_eof_condition(void*p,unsigned mode){(void)p;assert(mode==0);}
static void parlio_ll_rx_set_recv_bit_len(void*p,unsigned n){(void)p;assert(n%8==0);rx_bitlen=n;}
static void parlio_ll_rx_enable_clock_gating(void*p,bool en){(void)p;assert(en);gate_enabled=en;}
static void parlio_ll_rx_enable_timeout(void*p,bool en){(void)p;assert(!en);}
static void parlio_ll_enable_interrupt(void*p,unsigned mask,bool en){(void)p;assert(mask==PARLIO_LL_EVENT_RX_MASK&&!en);}
static void parlio_ll_clear_interrupt_status(void*p,unsigned mask){(void)p;assert(mask==PARLIO_LL_EVENT_RX_MASK);}
static void axi_dma_ll_rx_enable_owner_check(void*p,int ch,bool en){(void)p;assert(ch==2&&en);}
static void axi_dma_ll_rx_enable_descriptor_burst(void*p,int ch,bool en){(void)p;assert(ch==2&&en);}
static void axi_dma_ll_rx_set_burst_size(void*p,int ch,unsigned n){(void)p;assert(ch==2&&n==16);}
static void axi_dma_ll_rx_enable_auto_return(void*p,int ch,bool en){(void)p;assert(ch==2&&!en);}
static void axi_dma_ll_rx_enable_etm_task(void*p,int ch,bool en){(void)p;assert(ch==2&&!en);}
static void axi_dma_ll_rx_connect_to_periph(void*p,int ch,int periph,int id){(void)p;assert(ch==2&&periph==3&&id==3);}
static void axi_dma_ll_rx_set_desc_addr(void*p,int ch,uint32_t addr){(void)p;assert(ch==2&&addr==(uint32_t)(uintptr_t)active->desc);descriptor_ready=true;}
static void axi_dma_ll_rx_start(void*p,int ch){(void)p;assert(ch==2&&descriptor_ready&&!rx_clock);starts++;}
static uint32_t axi_dma_ll_rx_get_interrupt_status(void*p,int ch,bool raw)
{(void)p;assert(ch==2&&raw&&rx_clock&&rx_start&&!soft&&active->rx_started);for(unsigned i=0;i<8;i++)assert(!output[data_pins[i]]);return dma_raw;}

static uint32_t axi_dma_ll_rx_get_success_eof_desc_addr(void*p,int ch){(void)p;assert(ch==2);return fault==BAD_ADDR?0:(uint32_t)(uintptr_t)active->desc;}
#include "../hal/esp32p4/parlio_native_rx.inc"
static uint8_t storage[DMA_CAPACITY],descriptor_storage[128];
static void setup(hal_parlio_t*c,int mode){memset(c,0,sizeof*c);memset(&PARL_IO,0,sizeof PARL_IO);memset(&GPIO,0,sizeof GPIO);memset(output,0,sizeof output);memset(level,0,sizeof level);c->hz=1000000;c->rx_hz=1000000;c->rx_channel=2;c->payload=storage;c->desc=(dma_descriptor_align8_t*)descriptor_storage;active=c;fault=mode;irq_depth=0;now_us=0;rx_bitlen=starts=cache_reads=resets=reset_wait_calls=0;clock_signal=SIG_GPIO_OUT_IDX;rx_clock=rx_start=soft=descriptor_ready=stopped=gate_enabled=gate_armed=event_seen=task_seen=false;dma_raw=0;level[CLOCK_PIN]=level[DIR_PIN]=level[READY_PIN]=1;output[17]=1;}
static bool yes(void*p){(void)p;return true;}
int main(void)
{
    hal_parlio_t c;uint8_t out[HAL_PARLIO_MAX_RX];
    const size_t sizes[]={24,90,2074,2176};
    const unsigned rates[]={500000,1000000,2000000,5000000,8000000,10000000,16000000,20000000};
    for(unsigned rate=0;rate<8;rate++)for(unsigned k=0;k<4;k++) {
        setup(&c,NORMAL);c.rx_hz=rates[rate];memset(out,0x55,sizeof out);
        assert(!native_rx(&c,out,sizes[k],100000,NULL,NULL));
        assert(c.stats.last_rx_bytes==sizes[k]&&c.stats.rx_phase_verified&&starts==1&&resets==1&&cache_reads==2&&reset_wait_calls==2);
        assert(c.stats.rx_gate_verified&&c.stats.rx_gate_closed&&c.stats.gate_event_seen&&c.stats.gate_task_seen);
        for(unsigned i=0;i<sizes[k];i++)assert(out[i]==(uint8_t)(i^0xc3));
        assert(stopped&&!rx_clock&&!rx_start&&!soft&&level[CLOCK_PIN]==0&&!level[17]&&!output[17]);
        for(unsigned i=0;i<8;i++)assert(!output[data_pins[i]]);
    }
    const char *expected[]={NULL,"rx_eof_timeout","rx_register_update","rx_dma_error",
        "rx_descriptor_length","rx_descriptor_length","rx_descriptor_owner","rx_descriptor_eof",
        "rx_eof_address","rx_phase_completion","rx_descriptor_cache","rx_descriptor_error",
        "rx_payload_cache",NULL,"rx_eof_gate_unproven","rx_fifo_overflow","rx_gate_arm","dma_quarantined"};
    for(int mode=NO_EOF;mode<=STOP_FAIL;mode++) {
        if(mode==PREEMPTION)continue;
        setup(&c,mode);memset(out,0x55,sizeof out);
        assert(native_rx(&c,out,24,10000,NULL,NULL)<0);
        assert(!strcmp(c.stats.stage,expected[mode]));
        for(unsigned i=0;i<sizeof out;i++)assert(out[i]==0x55);
        assert(!rx_clock&&!rx_start&&!soft&&!level[17]&&!output[17]);
        if(mode==STOP_FAIL)assert(c.dma_quarantined&&cache_reads==0);
        if(mode==SHORT)assert(c.stats.last_rx_bytes==20);
        if(mode==GATE_FAIL||mode==GATE_FAIL_LATE)assert(!c.stats.rx_gate_closed);
    }
    setup(&c,PREEMPTION);
    assert(!native_rx(&c,out,24,10000,NULL,NULL)&&now_us>=3000);
    assert(c.stats.rx_gate_closed&&c.stats.gate_event_seen&&c.stats.gate_task_seen&&PARL_IO.int_raw.val==0);
    setup(&c,PREEMPTION);memset(out,0x55,sizeof out);
    assert(native_rx(&c,out,24,1000,NULL,NULL)==ERR_TIMEOUT);
    for(unsigned i=0;i<sizeof out;i++)assert(out[i]==0x55);
    setup(&c,NORMAL);level[17]=1;c.stats.rx_gate_verified=c.stats.rx_gate_closed=true;
    assert(native_rx(&c,out,24,10000,NULL,NULL)==ERR_BUSY&&!starts);
    assert(!c.stats.rx_gate_verified&&!c.stats.rx_gate_closed);
    assert(rx_reset_settle_us(500000)==6&&rx_reset_settle_us(1000000)==4);
    assert(!rx_reset_settle_us(0)&&!rx_reset_settle_us(999999)&&!rx_reset_settle_us(4000000));
    setup(&c,NORMAL);c.rx_hz=0;assert(native_rx(&c,out,24,10000,NULL,NULL)==ERR_INVAL&&starts==0&&reset_wait_calls==0);
    setup(&c,NORMAL);assert(native_rx(&c,out,24,10000,yes,NULL)==HAL_PARLIO_CANCELLED&&starts==0);
    setup(&c,NORMAL);level[CLOCK_PIN]=0;assert(native_rx(&c,out,24,10000,NULL,NULL)==ERR_BUSY&&starts==0);
    puts("Native RX model: 8 RX rates with unchanged1MHz TX, finite24/90/2074/2176 captures, hardware EOF gate under 3ms CPU preemption, failed gate/overflow rejection, strict EOF/count/phase/cache failures and cleanup passed");
}
