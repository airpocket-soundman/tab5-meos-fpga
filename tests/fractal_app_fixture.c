/* Standalone app API model. The GPU here is simulated, never hardware proof. */
#include <m5os/m5ex_api.h>
#include <m5os/dev_display.h>
#include <m5os/dev_fpga.h>
#include <m5os/dev_uart.h>
#include "../third_party/carrier_link/fractal_reference.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void app_main(const m5_api_t*,void*);
static const char*keys;static unsigned keypos,allocations,opens,closes,blits,gpus,ticks;static int fault;static bool mismatch_seen,error_seen,paused,frame,focused,foreign_focus;static unsigned begins,ends,logs;
static int open_dev(const char*p,uint32_t f){(void)f;opens++;return !strcmp(p,"/dev/uart")?1:!strcmp(p,"/dev/display")?2:!strcmp(p,"/dev/fpga")?3:-1;}
static int close_dev(int h){assert(h>=1&&h<=3);assert(!frame&&!paused);if(h==1)assert(!focused);closes++;return 0;}
static int write_dev(int h,const void*p,size_t n){assert(h==1&&paused&&!frame&&n<512);logs++;fwrite(p,1,n,stdout);return (int)n;}
static int read_dev(int h,void*p,size_t n){assert(h==1&&n==1&&focused&&!foreign_focus);*(char*)p=keys[keypos++];return 1;}
static void*alloc(size_t n){if(fault==3)return NULL;void*p=malloc(n);if(p)allocations++;return p;}
static void release(void*p){assert(p&&allocations);allocations--;free(p);}
static uint32_t uptime(void){return ticks++;}
static void sleep_ms(uint32_t t){ticks+=t;}
static int ioctl_dev(int h,uint32_t cmd,void*arg){
 if(h==1){assert(cmd==UART_IOC_FOCUS);bool acquire=*(bool*)arg;
  if(acquire&&(fault==8||foreign_focus))return -5;
  if(acquire)assert(!focused);else assert(focused);
  focused=acquire;return 0;
 }

 if(h==3){assert(!frame&&paused);assert(cmd==FPGA_IOC_FRACTAL_RENDER);fpga_fractal_request_t*r=arg;
  assert(r->size==sizeof*r&&r->version==1&&r->pixel_count==1024&&r->iterations==64);gpus++;
  if(fault==1)return -5;
  fractal_config c={r->x0,r->y0,r->dx,r->dy,r->cx,r->cy,r->iterations,r->mode};assert(fractal_reference_tile(&c,r->pixels,NULL));
  if(fault==2)r->pixels[99]^=1;
  r->elapsed_us=12345;r->engine_cycles=123;r->cycles_valid=true;return 0;
 }
 assert(h==2);
 if(cmd==DISPLAY_IOC_CONSOLE_PAUSE){bool p=*(bool*)arg;if(fault==7&&p)return -5;paused=p;return 0;}
 if(cmd==DISPLAY_IOC_BEGIN_FRAME){assert(paused&&!frame);if(fault==5)return -5;frame=true;begins++;return 0;}
 if(cmd==DISPLAY_IOC_END_FRAME){assert(frame);frame=false;ends++;return fault==6?-5:0;}

 if(cmd==DISPLAY_IOC_GET_INFO){display_info_t*i=arg;i->width=1280;i->height=720;return 0;}
 if(cmd==DISPLAY_IOC_CLEAR){assert(frame);return 0;}
 if(cmd==DISPLAY_IOC_DRAW_TEXT){assert(frame);display_text_t*t=arg;if(strstr(t->text,"COMPARE FAIL"))mismatch_seen=true;if(strstr(t->text,"failed"))error_seen=true;return 0;}
 if(cmd==DISPLAY_IOC_BLIT){assert(frame);display_blit_t*b=arg;assert(b->w==512&&b->h==16&&b->x==384);assert(b->y>=64&&b->y<=560);
  for(unsigned y=0;y<16;y++)for(unsigned x=0;x<512;x++)assert(b->pixels[y*512+x]==b->pixels[x/16*16]);
  blits++;return fault==4?-5:0;
 }assert(0);return -1;
}
int main(int argc,char**argv){fault=argc>1?atoi(argv[1]):0;foreign_focus=fault==9;keys=fault==1?"GQ":fault==2?"VQ":fault>=4?"Q":"GJVCMQ";
 m5_api_t a={.version=1,.size=sizeof a,.open=open_dev,.close=close_dev,.read=read_dev,.write=write_dev,.ioctl=ioctl_dev,.sleep_ms=sleep_ms,.uptime_ms=uptime,.alloc=alloc,.release=release,.snprintf=snprintf};app_main(&a,NULL);
 assert(!allocations&&opens==closes&&!frame&&!paused&&begins==ends&&!focused);
 if(!fault)assert(gpus==3&&blits==192&&!error_seen&&!mismatch_seen);
 if(fault==1)assert(gpus==1&&blits==32&&error_seen);
 if(fault==2)assert(gpus==1&&blits==32&&mismatch_seen);
 if(fault==3)assert(!gpus&&!blits);
 if(fault==4)assert(blits==1&&error_seen);
 if(fault==5)assert(!blits&&!begins&&logs==1);
 if(fault==6)assert(blits==32&&logs==1);
 if(fault==7)assert(!blits&&!begins&&!logs);
 if(fault==8||fault==9)assert(!blits&&!begins&&!logs&&opens==1);
 if(fault==9)assert(foreign_focus);
 puts("PASS standalone fractal API lifecycle/render/control/failure model");return 0;
}
