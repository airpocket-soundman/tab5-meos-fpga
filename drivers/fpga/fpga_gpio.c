/* Dock LINK v0 only. Carrier owns the pin/protocol specification. */
#include "fpga_gpio.h"
#include <m5os/hal_gpio.h>
#include <m5os/hal_time.h>
#include <m5os/hal_console.h>
#include <string.h>
static const int pins[]={18,19,5,3,4,2,48,47,16,45,51};
enum { CLK=16, READY=45, DIR=51 };
static int config(fpga_gpio_t*b,int pin,bool out,int v){return io_gpio_config(b->owner,pin,out,v,IO_PULL_NONE);}
static int level(fpga_gpio_t*b,int pin,int v){return io_gpio_write(b->owner,pin,v);}
static int inputs(fpga_gpio_t*b){int e=0;for(unsigned i=0;i<8;i++)if(config(b,pins[i],false,0))e=-1;return e;}
void fpga_gpio_close(fpga_gpio_t*b){
 if(!b)return;
 if(b->claimed){
  inputs(b); /* Release data before removing DIR/CLK drive. */
  config(b,CLK,false,0);config(b,DIR,false,0);config(b,READY,false,0);
 }
 if(b->owner)io_owner_close(b->owner);
 memset(b,0,sizeof *b);
}
int fpga_gpio_open(fpga_gpio_t*b,bool explicit_arm){
 if(!b||b->owner)return FPGA_LINK_INVALID;
 if(!explicit_arm||!M5OS_FPGA_DOCK_VERIFIED)return FPGA_LINK_INVALID;
 memset(b,0,sizeof *b);
 if(io_owner_create("fpga-dock-v0",&b->owner))return FPGA_LINK_BUSY;
 if(io_pins_claim(b->owner,pins,sizeof pins/sizeof pins[0],IO_USE_DIGITAL)){fpga_gpio_close(b);return FPGA_LINK_BUSY;}
 b->claimed=true;
 for(unsigned i=0;i<sizeof pins/sizeof pins[0];i++)if(config(b,pins[i],false,0))goto fail;
 if(config(b,CLK,true,0)||config(b,DIR,true,0))goto fail;
 /* Allow the FPGA 100ms idle watchdog to discard any partial prior frame. */
 hal_delay_us(110000);
 b->armed=true;return 0;
fail:fpga_gpio_close(b);return FPGA_LINK_IO;
}
static bool cancelled(void*p){fpga_gpio_t*b=p;int key=hal_console_getc();if(key==3||key==27)b->cancel=true;return b->cancel||!b->armed||!io_owner_valid(b->owner);}
static int delay(fpga_gpio_t*b,uint64_t deadline,unsigned us){
 if(cancelled(b)||hal_time_now_us()>=deadline)return -1;
 hal_delay_us(us);
 return cancelled(b)||hal_time_now_us()>=deadline?-1:0;
}
static int ready(fpga_gpio_t*b,uint64_t deadline,int expected){
 for(unsigned i=0;i<FPGA_LINK_POLL_LIMIT;i++){
  int v;if(cancelled(b)||hal_time_now_us()>=deadline||io_gpio_read(b->owner,READY,&v))return -1;
  if(v==expected)return 0;
  if(delay(b,deadline,100))return -1;
 }return -1;
}
static int pulse(fpga_gpio_t*b,uint64_t t){
 if(level(b,CLK,1)||delay(b,t,100))return -1;
 return level(b,CLK,0)||delay(b,t,100)?-1:0;
}
static int begin(void*p,uint64_t t){fpga_gpio_t*b=p;
 if(cancelled(b)||inputs(b)||level(b,CLK,0)||level(b,DIR,0)||delay(b,t,200)||ready(b,t,0))return -1;
 for(unsigned i=0;i<8;i++)if(config(b,pins[i],true,0))return -1;
 return 0;
}
static int write_bytes(void*p,const uint8_t*d,size_t n,uint64_t t){fpga_gpio_t*b=p;
 for(size_t j=0;j<n;j++){
  for(unsigned i=0;i<8;i++)if(level(b,pins[i],(d[j]>>i)&1))return -1;
  if(delay(b,t,100)||pulse(b,t))return -1;
 }return (int)n;
}
static int turn(void*p,uint64_t t){fpga_gpio_t*b=p;
 return inputs(b)||delay(b,t,200)||level(b,DIR,1)||delay(b,t,200)||ready(b,t,1)?-1:0;
}
static int read_bytes(void*p,uint8_t*d,size_t n,uint64_t t){fpga_gpio_t*b=p;
 for(size_t j=0;j<n;j++){
  if(delay(b,t,100))return -1;
  unsigned byte=0;
  for(unsigned i=0;i<8;i++){int v;if(io_gpio_read(b->owner,pins[i],&v))return -1;byte|=(unsigned)v<<i;}
  d[j]=(uint8_t)byte;if(pulse(b,t))return -1;
 }return (int)n;
}
static void finish(void*p,bool failed){fpga_gpio_t*b=p;
 if(failed){fpga_gpio_close(b);return;}
 if(inputs(b)||level(b,CLK,0)||level(b,DIR,0)){fpga_gpio_close(b);return;}
 hal_delay_us(200);
 /* Commands are one-shot: even success ends output drive and ownership. */
 fpga_gpio_close(b);
}
static uint64_t now(void*p){(void)p;return hal_time_now_us();}
static void idle(void*p){(void)p;hal_delay_us(100);}
const fpga_link_ops_t fpga_gpio_ops={begin,write_bytes,turn,read_bytes,finish,now,idle,cancelled};
