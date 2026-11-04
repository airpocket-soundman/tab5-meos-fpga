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
static void check_global(const char*);
static int write_dev(int h,const void*p,size_t n){assert(h==1&&paused&&!frame&&n<512);check_global(p);logs++;fwrite(p,1,n,stdout);return (int)n;}
static int32_t rx_timeout=-1;
static unsigned motion_stage,quiet_polls;
static uint16_t rendered[65536];static unsigned rendered_side,frame_blits;
static unsigned field(const char*s,const char*name){const char*p=strstr(s,name);assert(p);return (unsigned)strtoul(p+strlen(name),NULL,10);}
static int triangle_wave(unsigned p){unsigned t=p%8000;return t<4000?(int)t-2000:6000-(int)t;}
static void check_global(const char*line){
 unsigned side=field(line,"\"width\":"),phase=field(line,"\"phase_ms\":");
 assert(side==32||side==64||side==128||side==256);
 if(field(line,"\"error\":"))return;
 assert(rendered_side==side&&frame_blits==side);
 fractal_config c={-4096,-2048,132,132,-2949,819,64,1};
 if(strstr(line,"\"scene\":\"mandelbrot\"")){unsigned step=128u+(unsigned)(triangle_wave(phase)+2000)/32u;c.julia=0;c.dx=c.dy=(uint16_t)step;c.x0=(int16_t)(-3072-(int)step*16);c.y0=(int16_t)(-(int)step*16);}
 else {c.cx=(int16_t)(-2949+triangle_wave(phase)/4);c.cy=(int16_t)(819+triangle_wave(phase+2000)/4);}
 c.dx=(uint16_t)((unsigned)c.dx*32/side);c.dy=(uint16_t)((unsigned)c.dy*32/side);
 /* Global coordinate oracle deliberately does not use tile x0/scatter logic. */
 for(unsigned y=0;y<side;y++)for(unsigned x=0;x<side;x++)assert(rendered[y*side+x]==fractal_reference_pixel(&c,x,y,NULL));
}

static int read_dev(int h,void*p,size_t n){assert(h==1&&n==1&&focused&&!foreign_focus&&rx_timeout==10);
 if((fault==12||fault==14||fault==15)&&gpus==3&&logs==1){*(char*)p=fault==14?'R':fault==15?'V':'Q';return 1;}
 if(fault==13&&!logs){*(char*)p='Q';return 1;}
 if(fault==10){
  char key=0;
  if(motion_stage==0&&logs>=3){key=' ';motion_stage++;}
  else if(motion_stage==1&&logs==4&&++quiet_polls==10){key='G';motion_stage++;}
  else if(motion_stage==2&&logs==5){key='V';motion_stage++;}
  else if(motion_stage==3&&logs==6){key=' ';motion_stage++;}
  else if(motion_stage==4&&logs>=10)key='Q';
  if(!key){ticks+=10;return 0;}
  *(char*)p=key;return 1;
 }
 if(logs>keypos){*(char*)p=keys[keypos++];return 1;}
 ticks+=10;return 0;}
static void*alloc(size_t n){if(fault==3)return NULL;void*p=malloc(n);if(p)allocations++;return p;}
static void release(void*p){assert(p&&allocations);allocations--;free(p);}
static uint32_t uptime(void){return ticks++;}
static void sleep_ms(uint32_t t){ticks+=t;}
static int ioctl_dev(int h,uint32_t cmd,void*arg){
 if(h==1){if(cmd==UART_IOC_SET_RX_TIMEOUT){assert(focused&&*(int32_t*)arg==10);rx_timeout=10;return 0;}assert(cmd==UART_IOC_FOCUS);bool acquire=*(bool*)arg;
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
 if(cmd==DISPLAY_IOC_BEGIN_FRAME){assert(paused&&!frame);if(fault==5)return -5;frame=true;frame_blits=0;rendered_side=0;begins++;return 0;}
 if(cmd==DISPLAY_IOC_END_FRAME){assert(frame);frame=false;ends++;return fault==6?-5:0;}

 if(cmd==DISPLAY_IOC_GET_INFO){display_info_t*i=arg;i->width=1280;i->height=720;return 0;}
 if(cmd==DISPLAY_IOC_CLEAR){assert(frame);return 0;}
 if(cmd==DISPLAY_IOC_DRAW_TEXT){assert(frame);display_text_t*t=arg;if(strstr(t->text,"COMPARE FAIL"))mismatch_seen=true;if(strstr(t->text,"failed"))error_seen=true;return 0;}
 if(cmd==DISPLAY_IOC_BLIT){assert(frame);display_blit_t*b=arg;
  assert(b->w==512&&b->x==384&&(b->h==2||b->h==4||b->h==8||b->h==16));
  unsigned scale=(unsigned)b->h,side=512/scale,row=(b->y-64)/scale;assert(row<side);
  for(unsigned y=0;y<scale;y++)for(unsigned x=0;x<512;x++)assert(b->pixels[y*512+x]==b->pixels[x/scale*scale]);
  for(unsigned x=0;x<side;x++)rendered[row*side+x]=b->pixels[x*scale];
  rendered_side=side;frame_blits++;blits++;return fault==4?-5:0;
 }assert(0);return -1;
}
int main(int argc,char**argv){fault=argc>1?atoi(argv[1]):0;foreign_focus=fault==9;
 keys=fault==1?"GQ":fault==2?"VQ":fault==11?" GVRRRQ":(fault==12||fault==14||fault==15)?"GQ":fault>=4?"Q":" GJVCMRRRRQ";
 m5_api_t a={.version=1,.size=sizeof a,.open=open_dev,.close=close_dev,.read=read_dev,.write=write_dev,.ioctl=ioctl_dev,.sleep_ms=sleep_ms,.uptime_ms=uptime,.alloc=alloc,.release=release,.snprintf=snprintf};app_main(&a,NULL);
 assert(!allocations&&opens==closes&&!frame&&!paused&&begins==ends&&!focused);
 if(!fault)assert(gpus==48&&blits==1376&&!error_seen&&!mismatch_seen);
 if(fault==1)assert(gpus==1&&blits==128&&error_seen);
 if(fault==2)assert(gpus==16&&blits==128&&mismatch_seen);
 if(fault==3)assert(!gpus&&!blits);
 if(fault==4)assert(blits==1&&error_seen);
 if(fault==5)assert(!blits&&!begins&&logs==1);
 if(fault==6)assert(blits==128&&logs==1);
 if(fault==7)assert(!blits&&!begins&&!logs);
 if(fault==8||fault==9)assert(!blits&&!begins&&!logs&&opens==1);
 if(fault==9)assert(foreign_focus);
 if(fault==10)assert(logs==10&&gpus>=5&&!error_seen&&!mismatch_seen);
 if(fault==11)assert(logs==7&&gpus==101&&!error_seen&&!mismatch_seen);
 if(fault==12)assert(gpus==3&&logs==1&&blits==128);
 if(fault==13)assert(!gpus&&!logs&&!blits&&!begins);
 if(fault==14)assert(gpus==67&&logs==2&&blits==384);
 if(fault==15)assert(gpus==19&&logs==2&&blits==256);
 puts("PASS standalone fractal API lifecycle/render/control/failure model");return 0;
}
