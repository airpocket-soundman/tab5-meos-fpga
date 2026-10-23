#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "../../drivers/usb_dma/hal_usbh_dma.h"
#define CACHE_LINE 64
#define REG union { uint32_t val; }
typedef union { uint32_t val; struct { uint32_t mps:11,epnum:4,epdir:1,res:1,lspddev:1,eptype:2,ec:2,devaddr:7,oddfrm:1,chdis:1,chena:1; }; } usb_dwc_hcchar_reg_t;
typedef union { uint32_t val; struct { uint32_t xfersize:19,pktcnt:10,pid:2,dopng:1; }; } usb_dwc_hctsiz_reg_t;
typedef struct { usb_dwc_hcchar_reg_t hcchar_reg; REG hcsplt_reg; REG hcint_reg; REG hcintmsk_reg; usb_dwc_hctsiz_reg_t hctsiz_reg; REG hcdma_reg; } usb_dwc_host_chan_regs_t;
static struct { usb_dwc_host_chan_regs_t host_chans[16]; REG hcfg_reg; REG hfnum_reg; REG haintmsk_reg; REG gintmsk_reg; } device;
static struct { typeof(device) *dev; } s_hal={&device};
static bool s_ep_pid[16][32];
static uint64_t clock_us;
static usbh_speed_t speed=USBH_SPEED_HIGH;
static void *kmalloc_internal(size_t n) { return malloc(n); }
static void cache_write_back(void *p,size_t n) {(void)p;(void)n;}
static void cache_invalidate(void *p,size_t n) {(void)p;(void)n;}
static uint64_t hal_time_now_us(void) { for(unsigned i=0;i<16;i++) if(device.host_chans[i].hcint_reg.val==0x3fff) device.host_chans[i].hcint_reg.val=0; clock_us+=10; device.hfnum_reg.val=(uint32_t)(clock_us/125); return clock_us; }
usbh_speed_t hal_usbh_speed(void) {return speed;}
bool hal_usbh_connected(void) {return true;}
static uint32_t arch_irq_disable(void) {return 0;}
static void arch_irq_restore(uint32_t n) {(void)n;}
static void kprintf(const char *fmt,...) {(void)fmt;}
#include "../../hal/esp32p4/usb_buffer_dma.inc"
/* Fake MMIO has no W1C semantics. Inject a fresh interrupt value before every
 * reducer step; channel HALT explicitly clears CHENA as real hardware does. */
static void tick(uint64_t now) {for(unsigned i=0;i<16;i++) if(device.host_chans[i].hcint_reg.val==0x3fff) device.host_chans[i].hcint_reg.val=0; clock_us=now;device.hfnum_reg.val=(uint32_t)(now/125);}
static void event(unsigned id,uint32_t bits,unsigned remaining,uint64_t now) {
 tick(now); bdm_regs(id)->hcchar_reg.val &= ~BDM_CHENA;
 bdm_regs(id)->hcint_reg.val=bits|BDM_HALT;
 bdm_regs(id)->hctsiz_reg.xfersize=remaining;
 bdm_step(id,now);
}
static void reset(void) {
 memset(&device,0,sizeof device);memset(bdm_pipes,0,sizeof bdm_pipes);
 memset(bdm_routes,0,sizeof bdm_routes);memset(s_ep_pid,0,sizeof s_ep_pid);
 bdm_ready=false;speed=USBH_SPEED_HIGH;tick(0);assert(bdm_init());
}
static void split_in(void) {
 reset();uint8_t out[8]={0};bdm_route(2,USBH_SPEED_LOW,1,4);
 assert(bdm_open(2,2,0x81,8,3,10));assert(bdm_start(2,out,8,-1));
 assert(bdm_regs(2)->hcchar_reg.lspddev);assert(bdm_pipes[2].split);
 event(2,BDM_ACK,8,50);assert(bdm_pipes[2].done==0&&bdm_pipes[2].pid==0);
 assert(bdm_pipes[2].running && bdm_pipes[2].complete_split); /* CS armed immediately; ODDFRM schedules its microframe. */
 tick(300);bdm_step(2,300);assert(bdm_regs(2)->hcsplt_reg.val&BDM_COMPLETE);
 assert(bdm_regs(2)->hctsiz_reg.xfersize==8);
 memset(bdm_pipes[2].dma,0x45,8);event(2,BDM_XFRC,0,350);
 assert(bdm_pipes[2].result==8&&bdm_pipes[2].pid==2&&out[7]==0x45);
}
static void split_out_and_retry(void) {
 reset();uint8_t data[8]={1};bdm_route(1,USBH_SPEED_FULL,3,2);
 assert(bdm_open(0,1,0,64,0,0));assert(bdm_start(0,data,8,3));
 event(0,BDM_ACK,0,50);tick(300);bdm_step(0,300);
 assert(bdm_regs(0)->hctsiz_reg.xfersize==0&&bdm_regs(0)->hctsiz_reg.pktcnt==1);
 event(0,BDM_NYET,0,325);assert(bdm_pipes[0].complete_split&&bdm_pipes[0].done==0);
 assert(bdm_pipes[0].running && bdm_pipes[0].next_us==325); /* NYET retry also arms immediately. */
 tick(450);bdm_step(0,450);event(0,BDM_XFRC,0,475);
 assert(bdm_pipes[0].result==8&&bdm_pipes[0].pid==3);
}
static void bulk_short_and_error(void) {
 reset();uint8_t data[1024]={0};assert(bdm_open(1,1,0x81,512,2,0));
 assert(bdm_start(1,data,sizeof data,-1));memset(bdm_pipes[1].dma,0x11,512);
 event(1,BDM_XFRC,0,50);assert(bdm_pipes[1].done==512&&bdm_pipes[1].pid==2);
 memset(bdm_pipes[1].dma,0x22,128);event(1,BDM_XFRC,384,100);
 assert(bdm_pipes[1].result==640&&data[511]==0x11&&data[512]==0x22&&data[640]==0);
 assert(bdm_start(1,data,8,-1));event(1,BDM_XFRC,500,150);
 assert(bdm_pipes[1].result==USBH_ERR_SIZE); /* 12 received into eight-byte caller. */
 assert(bdm_start(1,data,8,-1));event(1,BDM_STALL,0,200);
 assert(bdm_pipes[1].result==USBH_ERR_STALL&&bdm_pipes[1].pid==0);
}
static void late_start_and_quarantine(void) {
 reset();bdm_route(2,USBH_SPEED_FULL,1,1);assert(bdm_open(2,2,0x81,8,3,10));
 tick(750);assert(bdm_start(2,NULL,8,-1));assert(!bdm_pipes[2].running);
 bdm_service();assert(bdm_pipes[2].running); /* Must cross late uframes despite ms caller polling. */
 bdm_regs(2)->hcint_reg.val=0;bdm_close(2);assert(bdm_pipes[2].quarantined);
 assert(!bdm_open(2,2,0x81,8,3,10));bdm_regs(2)->hcchar_reg.val=0;
 assert(bdm_open(2,2,0x81,8,3,10));assert(!bdm_pipes[2].quarantined);
}
static void nak_and_ping(void) {
 reset();uint8_t data[16]={0};assert(bdm_open(1,1,1,8,2,0));assert(bdm_start(1,data,16,-1));
 event(1,BDM_NAK,8,50);assert(bdm_pipes[1].active&&bdm_pipes[1].done==0&&bdm_pipes[1].pid==0);
 tick(175);bdm_step(1,175);bdm_regs(1)->hctsiz_reg.pktcnt=0;
 event(1,BDM_NYET,0,200);assert(bdm_pipes[1].done==8&&bdm_pipes[1].ping_phase);
 assert(bdm_regs(1)->hctsiz_reg.dopng);event(1,BDM_ACK,0,225);
 assert(!bdm_pipes[1].ping_phase&&bdm_regs(1)->hctsiz_reg.xfersize==8);
 event(1,BDM_XFRC,0,250);assert(bdm_pipes[1].result==16);
}
static void periodic_window(void) {
 reset();uint8_t out[8]={0};bdm_route(2,USBH_SPEED_FULL,1,2);
 assert(bdm_open(2,2,0x81,8,3,10));assert(bdm_start(2,out,8,-1));
 event(2,BDM_ACK,8,50);tick(300);bdm_step(2,300);
 event(2,BDM_NYET,8,325);tick(450);bdm_step(2,450);
 event(2,BDM_NYET,8,475);tick(600);bdm_step(2,600);
 event(2,BDM_NYET,8,625);
 assert(!bdm_pipes[2].complete_split&&!bdm_pipes[2].running);
 assert(bdm_pipes[2].active&&bdm_pipes[2].done==0&&bdm_pipes[2].pid==0);
 reset();bdm_route(2,USBH_SPEED_FULL,1,2);assert(bdm_open(2,2,0x81,8,3,10));
 assert(bdm_start(2,out,8,-1));event(2,BDM_ACK,8,50);
 bdm_pipes[2].running=false;bdm_regs(2)->hcchar_reg.val &= ~BDM_CHENA; tick(1300);bdm_step(2,1300); /* Missed frame: do not issue stale CS. */
 assert(!bdm_pipes[2].running&&!bdm_pipes[2].complete_split);
 assert(bdm_pipes[2].next_us==bdm_pipes[2].period_due_us);
}
int main(void) {periodic_window();split_in();split_out_and_retry();bulk_short_and_error();late_start_and_quarantine();nak_and_ping();puts("buffer DMA hardware-state traces PASS");return 0;}
