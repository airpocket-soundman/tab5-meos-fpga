/* Actual merged OSK polling with no display/I2C/IME lock nesting. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../../drivers/tab5kbd/osk.c"
static bool fake_paused,guard,busy,down,waking;static int px,py,clip=DISPLAY_HEIGHT,scrolls,live,feeds,refreshes,sleeps;static uint64_t clock_us;
bool display_overlay_begin(void){assert(!guard);if(busy)return false;guard=true;return true;}
void display_overlay_end(void){assert(guard);guard=false;}
void display_overlay_end_clip(int y){assert(guard);clip=y;guard=false;}
void display_clip_bottom(int y){assert(!guard);clip=y;}
bool display_is_suspended(void){return false;}
void display_fill_rect(int x,int y,int w,int h,uint16_t c){(void)x;(void)y;(void)w;(void)h;(void)c;assert(guard);}
void display_draw_text(int x,int y,const char*s,uint16_t f,uint16_t b,int z){(void)x;(void)y;(void)s;(void)f;(void)b;(void)z;assert(guard);}
void display_read_bitmap(int x,int y,int w,int h,uint16_t*p){(void)x;(void)y;assert(guard);memset(p,0,(size_t)w*h*2);}
void display_draw_bitmap(int x,int y,int w,int h,const uint16_t*p){(void)x;(void)y;(void)w;(void)h;(void)p;assert(guard);}
bool fbcon_is_paused(void){return fake_paused;}int fbcon_cell_px(void){return 16;}int fbcon_max_rows(void){return 45;}void fbcon_set_rows(int r){(void)r;assert(!guard);}void fbcon_scroll(int n){assert(!guard);scrolls+=n;}void fbcon_scroll_live(void){assert(!guard);live++;}
uint64_t hal_time_now_us(void){return clock_us;}
void task_sleep(uint32_t n){assert(!guard);clock_us+=(uint64_t)n*1000;sleeps++;}
int task_create_stack(const char*n,void(*f)(void*),void*a,uint8_t p,size_t s){(void)n;(void)f;(void)a;(void)p;(void)s;return 1;}
void *kmalloc_psram(size_t n){assert(!guard);return malloc(n);}
void ime_feed(const char*s){assert(!guard&&s&&*s);feeds++;}
void kprintf(const char*s,...){(void)s;}
void kconsole_request_refresh(void){assert(!guard);refreshes++;}
bool suspend_filter_input(void){bool w=waking;waking=false;return w;}uint32_t suspend_poll_ms(uint32_t n){return n;}void suspend_request(void){assert(!guard);}
void power_read(power_sample_t*p){assert(!guard);memset(p,0,sizeof*p);}
void battery_tracker_update(battery_tracker_t*t,int32_t u,uint64_t n){(void)t;(void)u;(void)n;}
void battery_status_make(const battery_tracker_t*t,const power_sample_t*p,uint32_t c,battery_status_t*s){(void)t;(void)p;(void)c;memset(s,0,sizeof*s);}
int battery_format(const battery_status_t*s,char*p,size_t n){(void)s;return snprintf(p,n,"Bat --");}
bool tab5_touch_init(void){return true;}bool tab5_touch_read(int*x,int*y){assert(!guard);*x=px;*y=py;return down;}void tab5_touch_stats(uint32_t*f,uint32_t*e,int*x,int*y,int*mx,int*my){*f=*e=0;*x=*y=*mx=*my=0;}int tab5_touch_map(void){return 1;}
static void touch(bool d,int x,int y){down=d;px=x;py=y;clock_us+=20000;osk_poll();assert(!guard);}
int main(void){
 assert(!s_visible);s_avail=true;layout();touch(true,100,100);touch(true,100,148);touch(false,100,148);assert(scrolls==3&&!live&&!feeds);
 touch(true,100,100);touch(false,100,100);assert(live==1);
 fake_paused=true;touch(false,0,0);touch(true,1270,100);assert(s_ov&&clip==s_top);touch(false,0,0);
 touch(true,130,s_top+4+ROW_PITCH+20);touch(false,0,0);assert(feeds==1);
 overlay_hide();assert(!s_ov&&clip==DISPLAY_HEIGHT&&refreshes==1);
 fake_paused=false;touch(false,0,0);assert(sleeps==1);clock_us=6000000;touch(false,0,0);
 waking=true;touch(true,100,100);touch(true,100,164);touch(false,0,0);assert(scrolls==3&&live==1);
 fake_paused=true;touch(false,0,0);busy=true;touch(true,1270,100);assert(!s_ov&&clip==DISPLAY_HEIGHT);
 osk_request_redraw();touch(false,0,0);assert(!s_repaint);
 free(s_saved);puts("PASS OSK merge: hidden default, console scroll/tap, app overlay/clip, key focus path, wake consumption, no lock across sleep/I2C/IME");return 0;
}
