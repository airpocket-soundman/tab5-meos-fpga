#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../drivers/fpga/fpga_gpio.h"
#include <m5os/hal_gpio.h>
#include <m5os/hal_time.h>
#ifdef NDEBUG
#error Assertions required
#endif
static const int data_pins[]={18,19,5,3,4,2,48,47};
static int out[55],levels[55],live,claims,closes,conflict,stuck,cancel_key;
static unsigned rxn,txn;static uint8_t req[8],response[8];static uint64_t clock_us,release_at;
uint64_t hal_time_now_us(void){return clock_us;}
void hal_delay_us(uint32_t us){clock_us+=us;if(us>=100000)rxn=txn=0;}
int hal_console_getc(void){int k=cancel_key;cancel_key=0;return k;}
int io_owner_create(const char*n,io_owner_t*o){(void)n;assert(!live);*o=1;live=1;return 0;}
bool io_owner_valid(io_owner_t o){return live&&o==1;}
int io_pins_claim(io_owner_t o,const int*p,size_t n,io_use_t use){assert(o==1&&n==11&&use==IO_USE_DIGITAL);(void)p;claims++;return conflict?-1:0;}
int io_owner_close(io_owner_t o){assert(o==1);memset(out,0,sizeof out);live=0;closes++;return 0;}
int io_gpio_config(io_owner_t o,int p,bool output,int v,io_pull_t pull){assert(o==1&&live&&pull==IO_PULL_NONE);if(p==45)assert(!output);if(output&&p!=16&&p!=51)assert(!levels[51]);out[p]=output;levels[p]=v;
 bool all=true;for(unsigned i=0;i<8;i++)if(out[data_pins[i]])all=false;if(all)release_at=clock_us;return 0;}
int io_gpio_write(io_owner_t o,int p,int v){assert(o==1&&live&&out[p]);
 if(p==51&&v){for(unsigned i=0;i<8;i++)assert(!out[data_pins[i]]);assert(clock_us-release_at>=200);}
 if(p==16&&v&&!levels[16]){
  if(!levels[51]){unsigned b=0;for(unsigned i=0;i<8;i++){assert(out[data_pins[i]]);b|=(unsigned)levels[data_pins[i]]<<i;}assert(rxn<8);req[rxn++]=(uint8_t)b;
   if(rxn==8){response[0]=0x5a;response[1]=0;response[2]=req[2];response[3]=2;response[4]=req[1]==1?0x54:req[1]==2?req[4]:(uint8_t)~req[4];response[5]=req[1]==1?0x50:req[1]==2?req[5]:(uint8_t)~req[5];uint16_t c=fpga_link_crc16(response,6);response[6]=(uint8_t)c;response[7]=(uint8_t)(c>>8);}
  }else txn++;
 }levels[p]=v;return 0;}
int io_gpio_read(io_owner_t o,int p,int*v){assert(o==1&&live);if(p==45){*v=stuck?1:rxn==8&&txn<8;return 0;}for(unsigned i=0;i<8;i++)if(p==data_pins[i]){assert(!out[p]&&levels[51]&&txn<8);*v=(response[txn]>>i)&1;return 0;}return -1;}
static void released(void){assert(!live);for(unsigned i=0;i<55;i++)assert(!out[i]);}
int main(void){
 fpga_gpio_t b={0};assert(fpga_gpio_open(&b,false)<0);assert(!claims);
#if !M5OS_FPGA_DOCK_VERIFIED
 assert(fpga_gpio_open(&b,true)<0);assert(!claims);puts("PASS unverified ARM gate: zero IO activity");
#else
 conflict=1;assert(fpga_gpio_open(&b,true)<0);released();conflict=0;
 fpga_link_t link={.ops=&fpga_gpio_ops,.context=&b};uint8_t r[2];
 for(unsigned i=0;i<513;i++){assert(!fpga_gpio_open(&b,true));assert(fpga_gpio_open(&b,true)<0);uint8_t v=(uint8_t)((i-1)/2);int op=i==0?1:(i&1)?2:3;assert(!fpga_link_exchange(&link,op,v,v^0xa5,r,100000));released();}
 assert(!fpga_gpio_open(&b,true));stuck=1;assert(fpga_link_exchange(&link,1,0,0,r,1000)<0);released();stuck=0;
 assert(!fpga_gpio_open(&b,true));cancel_key=3;assert(fpga_link_exchange(&link,1,0,0,r,100000)<0);fpga_gpio_close(&b);released();fpga_gpio_close(&b);released();
 puts("PASS 513 GPIO-model exchanges, ownership conflict, reentry, timeout, cancel, turnaround and input cleanup");
#endif
 return 0;
}

