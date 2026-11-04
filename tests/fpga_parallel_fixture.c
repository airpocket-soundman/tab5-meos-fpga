#include "../drivers/fpga/fpga_parallel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef NDEBUG
#error Assertions required
#endif
static int outputs[55],levels[55],live,claimed,conflict,fail_tx,fail_rx,fail_open,ready_value;
static bool owner_protected,fail_close;
static unsigned owners,opens,txs,rxs;static uint64_t ticks;static uint8_t response[2058];static size_t gpio_index;
static bool gpio_tagged;static uint32_t stream_offset;static int stream_fault;
static bool bulk_expected,ready_before_dir;static uint64_t clock_high_at,dir_high_at;
static hal_parlio_stream_status_t stream_status;
static const int data[]={18,19,5,3,4,2,48,47};
struct hal_parlio{hal_parlio_stats_t stats;};static struct hal_parlio hal;
void *kmalloc(size_t n){return malloc(n);}
void kfree(void *p){free(p);}
int hal_parlio_stream_begin(hal_parlio_t*h,hal_parlio_stream_block_t cb,void*p,uint64_t t){
 assert(h==&hal&&t>ticks&&levels[16]&&levels[51]&&ready_value&&!outputs[17]);
 assert(ready_before_dir&&dir_high_at-clock_high_at==2&&ticks-dir_high_at==2);
 static uint8_t block[1024];
 for(unsigned b=0;b<4;b++){
  memset(block,0xc3,sizeof block);
  for(unsigned i=0;i<1024;i++){unsigned at=b*1024+i;if(at>=5&&at<5+sizeof response)block[i]=response[at-5];}
  if(!cb(p,block,sizeof block,stream_offset)){stream_status.fatal=true;break;}
  stream_offset+=1024;
 }
 stream_status.blocks+=4;stream_status.bytes+=4096;stream_status.epoch_bytes=4096;
 stream_status.closed=true;stream_status.ready_latched=true;ready_value=0;
 if(stream_fault==1)stream_status.pending=true;
 return 0;
}
int hal_parlio_stream_pause(hal_parlio_t*h,uint64_t t){assert(h==&hal);(void)t;levels[16]=0;return stream_fault==2?ERR_IO:0;}
void hal_parlio_stream_status(hal_parlio_t*h,hal_parlio_stream_status_t*s){assert(h==&hal);*s=stream_status;}
uint64_t hal_time_now_us(void){return ticks;}
void hal_delay_us(uint32_t us){ticks+=us;}
int io_owner_create(const char*n,io_owner_t*out){(void)n;assert(!live);owners++;live=1;*out=1;return 0;}
int io_owner_close(io_owner_t o){assert(o==1);if(owner_protected)return ERR_BUSY;live=claimed=0;memset(outputs,0,sizeof outputs);return 0;}
int io_owner_protect(io_owner_t o,bool enabled){assert(o==1&&live);owner_protected=enabled;return 0;}
int io_pins_claim(io_owner_t o,const int*p,size_t n,io_use_t use){assert(o==1&&n==15&&use==IO_USE_DIGITAL);(void)p;if(conflict)return ERR_BUSY;claimed=1;return 0;}
int io_gpio_config(io_owner_t o,int p,bool output,int value,io_pull_t pull){assert(o==1&&claimed&&pull==IO_PULL_NONE);if(p==45||p==52||p==6||p==7)assert(!output);if(p==17&&output)assert(value==0&&levels[16]&&levels[51]&&ready_value);if(bulk_expected&&p==16&&output&&value){assert(ready_before_dir&&!levels[51]);clock_high_at=ticks;}outputs[p]=output;levels[p]=value;return 0;}
int io_gpio_write(io_owner_t o,int p,int v){assert(o==1&&claimed);if(!outputs[p])return ERR_PERM;if(bulk_expected&&p==51&&v){assert(ready_before_dir&&levels[16]);dir_high_at=ticks;}if(p==16&&v&&!levels[p]&&levels[51]){gpio_index++;if(gpio_index>=8)ready_value=0;}levels[p]=v;return 0;}
int io_gpio_read(io_owner_t o,int p,int*v){assert(o==1&&claimed);if(p==45){*v=ready_value;if(bulk_expected&&ready_value){assert(!levels[51]&&!levels[16]);ready_before_dir=true;}return 0;}for(unsigned i=0;i<8;i++)if(p==data[i]){assert(!outputs[p]);unsigned at=gpio_index;uint8_t value=gpio_tagged?(at<8?0xc3:at<16?response[at-8]:0):response[at&7];*v=(value>>i)&1;return 0;}return ERR_INVAL;}
int hal_parlio_open(uint32_t hz,hal_parlio_t**out){assert(hz==1000000);opens++;if(fail_open)return ERR_BUSY;memset(&hal,0,sizeof hal);memset(&stream_status,0,sizeof stream_status);stream_offset=0;*out=&hal;return 0;}
bool hal_parlio_close(hal_parlio_t*h){assert(h==&hal&&live&&owner_protected);if(fail_close){h->stats.dma_quarantined=true;return false;}return true;}
void hal_parlio_get_stats(const hal_parlio_t*h,hal_parlio_stats_t*s){*s=h->stats;}
int hal_parlio_tx(hal_parlio_t*h,const uint8_t*q,size_t n,uint64_t end,hal_parlio_cancel_t cancel,void*ctx){assert(h==&hal&&n==8&&end>ticks&&!cancel(ctx));assert(levels[16]==0&&levels[51]==0);for(unsigned i=0;i<8;i++)assert(outputs[data[i]]);txs++;h->stats.tx_attempts++;for(unsigned i=0;i<8;i++)outputs[data[i]]=0;levels[16]=0;if(fail_tx)return ERR_IO;
 response[0]=0x5a;response[1]=0;response[2]=q[2];response[3]=2;unsigned op=q[1]&0x7f;response[4]=op==4?8:op==1?0x54:op==2?q[4]:(uint8_t)~q[4];response[5]=op==4?1:op==1?0x50:op==2?q[5]:(uint8_t)~q[5];
 if(op==0x3b)response[4]=response[5]=0;
 uint16_t c=fpga_link_crc16(response,6);response[6]=(uint8_t)c;response[7]=(uint8_t)(c>>8);
 if(op==0x3b){for(unsigned i=8;i<2056;i++)response[i]=(uint8_t)i;for(unsigned i=8;i<2056;i++){c^=(uint16_t)response[i]<<8;for(unsigned j=0;j<8;j++)c=c&0x8000?(uint16_t)((c<<1)^0x1021):(uint16_t)(c<<1);}response[2056]=(uint8_t)c;response[2057]=(uint8_t)(c>>8);}
 bulk_expected=op==0x3b;ready_before_dir=false;
 gpio_tagged=!!(q[1]&128);gpio_index=0;ready_value=!(bulk_expected&&stream_fault==3);h->stats.tx_completed++;ticks+=20;return 0;}
int hal_parlio_rx(hal_parlio_t*h,uint8_t*d,size_t cap,size_t*got,uint64_t end,hal_parlio_cancel_t cancel,void*ctx){assert(h==&hal&&end>ticks&&!cancel(ctx)&&levels[16]==1&&levels[51]==1);for(unsigned i=0;i<8;i++)assert(!outputs[data[i]]);assert(outputs[17]&&levels[17]==0);rxs++;*got=0;if(fail_rx)return fail_rx;assert(cap==24);memset(d,0xc3,8);memcpy(d+8,response,8);memset(d+16,0,cap-16);*got=cap;ready_value=0;h->stats.rx_completed++;ticks+=24;return 0;}
static bool cancel_now(void*p){return *(bool*)p;}
static void safe(void){for(unsigned i=0;i<55;i++)assert(!outputs[i]);}
int main(void){fpga_parallel_t bus={0};assert(fpga_parallel_open(&bus,"wrong",1000000)==FPGA_LINK_INVALID&&!owners);assert(fpga_parallel_open(&bus,"dock-jumper-v0",999)==FPGA_LINK_INVALID&&!owners);
 conflict=1;assert(fpga_parallel_open(&bus,"dock-jumper-v0",1000000)==FPGA_LINK_BUSY);assert(!opens&&!live);conflict=0;
 fail_open=1;assert(fpga_parallel_open(&bus,"dock-jumper-v0",1000000)==FPGA_LINK_BUSY);assert(!live);fail_open=0;
 assert(!fpga_parallel_open(&bus,"dock-jumper-v0",1000000));assert(fpga_parallel_open(&bus,"dock-jumper-v0",1000000)==FPGA_LINK_BUSY);
 fpga_wire_workspace_t ws;fpga_wire_t wire;assert(!fpga_wire_init(&wire,&fpga_parallel_wire_ops,&bus,&ws));assert(!fpga_wire_caps(&wire,100000));safe();assert(wire.sync_capable&&rxs==0);
 bus.native_rx=true;uint8_t body[8];assert(!fpga_wire_command(&wire,1,0,0,true,body,8,100000));assert(body[4]==0x54&&body[5]==0x50&&rxs==1);safe();
 bus.tagged=true;fpga_link_t old={.ops=&fpga_parallel_link_ops,.context=&bus};uint8_t pair[2];assert(!fpga_link_exchange(&old,2,0x55,0xaa,pair,100000));assert(pair[0]==0x55&&pair[1]==0xaa);safe();fpga_parallel_close(&bus);assert(!live);
 memset(&bus,0,sizeof bus);assert(!fpga_parallel_open(&bus,"dock-jumper-v0",1000000));fail_tx=1;size_t got=99;uint8_t req[8],rx[24];fpga_link_request(req,1,0,0,0);assert(fpga_parallel_wire_ops.exchange(&bus,req,8,rx,24,&got,ticks+100000)==FPGA_LINK_IO&&got==0);assert(bus.failed);safe();fpga_parallel_close(&bus);fail_tx=0;
 memset(&bus,0,sizeof bus);ready_value=0;assert(!fpga_parallel_open(&bus,"dock-jumper-v0",1000000));bus.native_rx=true;fail_rx=ERR_NOSYS;got=99;assert(fpga_parallel_wire_ops.exchange(&bus,req,8,rx,24,&got,ticks+100000)==FPGA_LINK_UNSUPPORTED&&got==0);safe();fpga_parallel_close(&bus);fail_rx=0;
 memset(&bus,0,sizeof bus);ready_value=0;assert(!fpga_parallel_open(&bus,"dock-jumper-v0",1000000));bool stop=true;bus.cancel=cancel_now;bus.cancel_context=&stop;unsigned before=txs;got=99;assert(fpga_parallel_wire_ops.exchange(&bus,req,8,rx,24,&got,ticks+100000)==FPGA_LINK_CANCELLED&&got==0&&txs==before);safe();fpga_parallel_close(&bus);
 memset(&bus,0,sizeof bus);ready_value=0;assert(!fpga_parallel_open(&bus,"dock-jumper-v0",1000000));
 fail_close=true;fpga_parallel_close(&bus);
 assert(live&&claimed&&owner_protected&&bus.owner_guarded&&bus.native&&bus.failed&&bus.error==FPGA_LINK_BUSY);
 assert(bus.stats.dma_quarantined&&io_owner_close(bus.owner)==ERR_BUSY);safe();
 assert(fpga_parallel_open(&bus,"dock-jumper-v0",1000000)==FPGA_LINK_BUSY);
 /* Only a later explicitly proven HAL teardown may release the pin lease. */
 fail_close=false;fpga_parallel_close(&bus);assert(!live&&!owner_protected&&!bus.owner_guarded&&!bus.native);safe();
 for(int fault=0;fault<=3;fault++){
 memset(&bus,0,sizeof bus);ready_value=0;assert(!fpga_parallel_open(&bus,"dock-jumper-v0",1000000));assert(!fpga_parallel_continuous(&bus));
 assert(!fpga_wire_init(&wire,&fpga_parallel_stream_wire_ops,&bus,&ws));wire.sync_capable=wire.tile_capable=true;
 assert(!fpga_wire_command(&wire,1,0,0,true,body,8,100000)&&body[4]==0x54);
 static uint8_t tile[2058];stream_fault=fault;
 int e=fpga_wire_tile(&wire,0,true,tile,100000);
 if(fault){assert(e<0&&bus.failed);if(fault==3)assert(e==FPGA_LINK_TIMEOUT&&!ready_before_dir&&stream_status.bytes==0);}else {assert(!e&&tile[8]==8&&tile[2055]==7);assert(!fpga_wire_tile(&wire,0,true,tile,100000));assert(bus.stream_stats.bytes==8192);}
 fpga_stream_t *retained=bus.stream;fail_close=true;fpga_parallel_close(&bus);assert(bus.stream==retained&&bus.native&&owner_protected);
 fail_close=false;fpga_parallel_close(&bus);assert(!bus.stream&&!bus.native&&!live);safe();
 }stream_fault=0;
 puts("PASS parallel ownership, hybrid/finite/continuous paths, exact normalized readback, persistent epochs, pause/pending failures, protected queue quarantine and no fallback");return 0;}
