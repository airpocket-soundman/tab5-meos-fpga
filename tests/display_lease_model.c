#include <assert.h>
#include <stdio.h>
#include "../drivers/lcd/display_dev.c"
static task_t a={.id=1,.state=TASK_RUNNING},b={.id=2,.state=TASK_RUNNING};
static task_t *self=&a;static bool paused,available=true;static unsigned begins,ends,draws;
static bool render_busy;
bool display_render_busy(void){return render_busy;}
task_t *task_current(void){return self;}
bool fbcon_is_paused(void){return paused;}
void fbcon_pause(bool p){paused=p;}
bool display_begin_frame(void){begins++;return available;}
bool display_end_frame(void){ends++;return true;}
int device_register(device_t*d){assert(d->required_perm==APP_PERM_DISPLAY);return 0;}
void board_backlight(uint8_t v){(void)v;}
void display_clear(uint16_t c){(void)c;draws++;}
void display_fill_rect(int x,int y,int w,int h,uint16_t c){(void)x;(void)y;(void)w;(void)h;(void)c;draws++;}
void display_draw_rect(int x,int y,int w,int h,uint16_t c){(void)x;(void)y;(void)w;(void)h;(void)c;draws++;}
void display_draw_char(int x,int y,char c,uint16_t fg,uint16_t bg,int s){(void)x;(void)y;(void)c;(void)fg;(void)bg;(void)s;draws++;}
void display_draw_text(int x,int y,const char*t,uint16_t fg,uint16_t bg,int s){(void)x;(void)y;(void)t;(void)fg;(void)bg;(void)s;draws++;}
void display_draw_bitmap(int x,int y,int w,int h,const uint16_t*p){(void)x;(void)y;(void)w;(void)h;(void)p;draws++;}
void display_draw_bitmap_scaled(int x,int y,int w,int h,const uint16_t*p,int scale){
 assert(x==3&&y==4&&w==32&&h==32&&p&&scale==4);draws++;
}
int main(void){
 display_dev_register();bool on=true,off=false;uint16_t color=0;
 assert(!display_suspend_busy());render_busy=true;assert(display_suspend_busy());render_busy=false;
 display_scaled_blit_t scaled={.image={.x=3,.y=4,.w=32,.h=32,.pixels=&color},.scale=4};
 assert(!display_ioctl(NULL,DISPLAY_IOC_BLIT_SCALED,&scaled));assert(draws==1);draws=0;
 scaled.scale=0;assert(display_ioctl(NULL,DISPLAY_IOC_BLIT_SCALED,&scaled)==ERR_INVAL&&!draws);
 scaled.scale=17;assert(display_ioctl(NULL,DISPLAY_IOC_BLIT_SCALED,&scaled)==ERR_INVAL&&!draws);scaled.scale=4;
 assert(!display_ioctl(NULL,DISPLAY_IOC_CONSOLE_PAUSE,&on)&&paused);
 assert(display_suspend_busy());
 assert(!display_ioctl(NULL,DISPLAY_IOC_BEGIN_FRAME,NULL)&&lease_frame);
 self=&b;assert(display_ioctl(NULL,DISPLAY_IOC_CLEAR,&color)==ERR_BUSY);assert(display_write(NULL,"x",1)==ERR_BUSY);assert(draws==0);
 assert(!display_close(NULL)&&paused&&ends==0); /* Foreign close cannot steal a live lease. */
 self=&a;assert(display_ioctl(NULL,DISPLAY_IOC_CONSOLE_PAUSE,&off)==ERR_BUSY);
 assert(!display_ioctl(NULL,DISPLAY_IOC_END_FRAME,NULL)&&ends==1&&!lease_frame&&paused);
 assert(!display_ioctl(NULL,DISPLAY_IOC_CONSOLE_PAUSE,&off)&&!paused&&!lease_owner);
 available=false;assert(display_ioctl(NULL,DISPLAY_IOC_BEGIN_FRAME,NULL)==ERR_BUSY&&!lease_owner);available=true;
 paused=true;assert(!display_ioctl(NULL,DISPLAY_IOC_CONSOLE_PAUSE,&on));assert(!display_close(NULL)&&paused); /* Restore prior pause=true. */
 paused=false;assert(!display_ioctl(NULL,DISPLAY_IOC_CONSOLE_PAUSE,&on));assert(!display_ioctl(NULL,DISPLAY_IOC_BEGIN_FRAME,NULL));assert(!display_close(NULL)&&!paused&&ends==2&&!lease_owner);
 assert(!display_ioctl(NULL,DISPLAY_IOC_CONSOLE_PAUSE,&on));assert(!display_ioctl(NULL,DISPLAY_IOC_BEGIN_FRAME,NULL));a.state=TASK_TERMINATED;self=&b;assert(!display_close(NULL)&&!paused&&!lease_owner&&ends==2); /* Killed frame uses backend's existing orphan recovery, not foreign END. */
 a.state=TASK_RUNNING;self=&a;assert(!display_ioctl(NULL,DISPLAY_IOC_CONSOLE_PAUSE,&on));a.id=9;self=&b;assert(!display_close(NULL)&&!paused&&!lease_owner); /* TCB reuse cannot retain old lease. */
 puts("Display lease: batch pairing, other-writer BUSY, foreign-close isolation, prior console state, close/dead-task/TCB reuse cleanup passed");return 0;
}
