#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define PARLIO_LL_EVENT_RX_FIFO_FULL 2u
#define READY_PIN 45
typedef void *intr_handle_t;
typedef struct descriptor {struct{unsigned size,length,owner;}dw0;void *buffer;struct descriptor *next;}dma_descriptor_align8_t;
typedef bool (*hal_parlio_stream_block_t)(void*,const uint8_t*,size_t,uint32_t);
struct native_stream;
typedef struct{unsigned rx_hz;int rx_channel;struct native_stream *stream;}hal_parlio_t;
static struct{struct{uint32_t val;}int_raw;bool soft;}PARL_IO;
static int AXI_DMA,GPIO;
static uint32_t dma_raw,clock_us;static bool ready,cache_failure,publish_failure;
static uint32_t published;
static uint32_t axi_dma_ll_rx_get_interrupt_status(void*h,int ch,bool raw){(void)h;assert(ch==2&&raw);return dma_raw;}
static void axi_dma_ll_rx_clear_interrupt_status(void*h,int ch,uint32_t raw){(void)h;assert(ch==2);dma_raw&=~raw;}
static void parlio_ll_rx_start_soft_recv(void*h,bool on){(void)h;PARL_IO.soft=on;}
static uint64_t hal_time_now_us(void){return clock_us;}
static int sync_m2c(void*p,size_t n){assert(p&&n);return cache_failure?-1:0;}
static int gpio_ll_get_level(void*h,int pin){(void)h;assert(pin==45);return ready;}
/* ACTUAL_ISR */
static bool publish(void*ctx,const uint8_t*b,size_t n,uint32_t offset){(void)ctx;assert(n==1024);assert(offset==published);assert(b[0]==(uint8_t)(offset/1024));if(publish_failure)return false;published+=1024;return true;}
static struct native_stream st;static hal_parlio_t c;static dma_descriptor_align8_t ds[4];static uint8_t payload[4096];
static void reset(void){memset(&st,0,sizeof st);memset(&PARL_IO,0,sizeof PARL_IO);memset(ds,0,sizeof ds);c=(hal_parlio_t){.rx_hz=20000000,.rx_channel=2,.stream=&st};st.callback=publish;st.active=true;st.started=true;PARL_IO.soft=true;cache_failure=publish_failure=false;dma_raw=clock_us=published=0;ready=true;for(unsigned i=0;i<4;i++){st.desc[i]=&ds[i];ds[i].buffer=payload+i*1024;ds[i].next=&ds[(i+1)%4];ds[i].dw0.size=ds[i].dw0.length=1024;}st.payload=payload;}
static void complete(void){unsigned i=(st.bytes/1024)%4;memset(payload+i*1024,(uint8_t)(st.bytes/1024),1024);clock_us+=51;dma_raw=1u|(i==3?2u:0u);native_stream_isr(&c);}
int main(void){
 reset();assert(!stream_safe_tx(&c));for(unsigned i=0;i<4;i++)complete();assert(st.bytes==4096&&st.eofs==1&&!st.closed&&PARL_IO.soft); /* EOF does not stop ring. */
 complete();assert(st.closed&&!PARL_IO.soft&&st.stop_reason==2&&st.bytes==5120);assert(stream_safe_tx(&c));
 complete();assert(st.late_bytes==1024&&published==6144); /* Late actual block is retained. */
 reset();ready=false;complete();assert(st.ready_latched&&!st.closed&&PARL_IO.soft);complete();assert(st.closed&&st.stop_reason==1&&st.bytes==2048); /* READY latch plus one real block. */
 reset();cache_failure=true;complete();assert(st.fatal&&st.closed&&st.bytes==0&&published==0&&st.stop_reason==6);
 reset();publish_failure=true;complete();assert(st.fatal&&st.bytes==0&&st.stop_reason==7);
 reset();ds[0].dw0.length=1023;complete();assert(st.fatal&&published==0);
 reset();dma_raw=3;native_stream_isr(&c);assert(st.fatal&&st.stop_reason==4&&published==0); /* Coalesced EOF at wrong ring position. */
 reset();dma_raw=4;native_stream_isr(&c);assert(st.fatal&&st.stop_reason==3&&published==0);
 reset();PARL_IO.int_raw.val=2;complete();assert(st.fatal&&published==0);
 reset();complete();clock_us+=154;complete();assert(st.fatal&&st.stop_reason==5&&published==1024); /* Missed buffer lease poisons. */
 reset();st.active=false;assert(stream_safe_tx(&c));st.fatal=true;assert(!stream_safe_tx(&c));
 puts("Native ring ISR: normal EOF persistence, READY+tail/budget closure, monotonic copies, late carry, FIFO/DMA/cache/descriptor/gap loss rejection passed");return 0;
}
