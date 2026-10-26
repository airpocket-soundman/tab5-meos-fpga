#include "../user/image/image_app.h"
#include "../user/image/image_geometry.h"
#include "../user/camera/camera_app.h"
#include <m5os/display.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error Assertions must remain enabled
#endif
static int live,frames,ends,lines,key_index,keys[4],row_fail,bad_image,draw_fail,no_batch;
static bool in_frame;
static uint16_t expected[4]={0xf800,0x07e0,0x001f,0xffff};
void kprintf(const char*f,...){(void)f;}
void *kmalloc_psram(size_t n){if(row_fail)return NULL;live++;return malloc(n);}
void *kmalloc(size_t n){if(row_fail==2)return NULL;live++;return malloc(n);}
void kfree(void*p){if(p){live--;free(p);}}
bool camera_load_bmp(const char*p,uint16_t **out,int*w,int*h){(void)p;*out=NULL;if(bad_image)return false;*out=malloc(8);memcpy(*out,expected,8);*w=*h=2;live++;return true;}
bool camera_verify_bmp_file(const char*p,camera_bmp_stats_t*out){(void)p;memset(out,0,sizeof(*out));out->width=out->height=2;out->pixel_count=4;return !bad_image;}
bool display_begin_frame(void){assert(!in_frame);frames++;in_frame=!no_batch;return !no_batch;}
bool display_end_frame(void){assert(in_frame);in_frame=false;ends++;return !draw_fail;}
void display_clear(uint16_t c){assert(c==COLOR_BLACK);}
void display_draw_text(int x,int y,const char*s,uint16_t f,uint16_t b,int scale){(void)s;(void)f;(void)b;(void)scale;assert(x>=0&&y>=0&&x<DISPLAY_WIDTH&&y<DISPLAY_HEIGHT);}
void display_draw_bitmap(int x,int y,int w,int h,const uint16_t*p){
 assert(x>=0&&y>=0&&w>0&&h==1&&x+w<=DISPLAY_WIDTH&&y+h<=DISPLAY_HEIGHT);
 int dh=w; /* fixture is square; aspect-preserving viewport is square too */
 int dy=32+((DISPLAY_HEIGHT-64)-dh)/2;
 for(int i=0;i<w;i++)assert(p[i]==expected[((y-dy)*2/dh)*2+i*2/w]);lines++;
}
int dq_getkey(void){assert(!in_frame);assert(key_index<4);return keys[key_index++];}
static void reset(void){assert(live==0&&!in_frame);frames=ends=lines=key_index=row_fail=bad_image=draw_fail=no_batch=0;keys[0]='q';keys[1]=keys[2]=keys[3]=0;}
int main(void){
 int w,h;assert(image_fit_size(640,360,1000,1000,true,&w,&h)&&w==1000&&h==562);
 assert(image_fit_size(100,200,100,100,true,&w,&h)&&w==50&&h==100);
 assert(image_fit_size(3,2,100,100,true,&w,&h)&&w==100&&h==66);
 assert(image_fit_size(2,2,100,100,false,&w,&h)&&w==2&&h==2);
 assert(image_fit_size(2147483647,1,100,100,true,&w,&h)&&w==100&&h==1);
 assert(!image_fit_size(0,2,100,100,true,&w,&h));assert(!image_fit_size(2,2,100,100,true,NULL,&h));
 reset();assert(image_file_info("fixture")==0&&live==0);
 assert(image_view_file("fixture")==0&&live==0&&frames==1&&ends==1&&lines==656);
 reset();keys[0]='z';keys[1]='q';assert(image_view_file("fixture")==0&&frames==2&&lines==658&&live==0);
 reset();keys[0]='x';keys[1]=3;assert(image_view_file("fixture")==0&&frames==1&&key_index==2&&live==0);
 reset();bad_image=1;assert(image_view_file("bad")==1&&frames==0&&live==0);assert(image_file_info("bad")==1);
 reset();row_fail=1;assert(image_view_file("fixture")==0&&live==0);
 reset();row_fail=2;assert(image_view_file("fixture")==1&&live==0&&frames==0);
 reset();no_batch=1;assert(image_view_file("fixture")==0&&live==0&&ends==0);
 reset();draw_fail=1;assert(image_view_file("fixture")==1&&live==0&&key_index==0);
 puts("PASS image geometry, pixel scaling, enlargement, close/cancel, decode/allocation/presentation failures and cleanup");
 return 0;
}
