/* Application lifecycle model: no physical transport or LCD. */
#include "../drivers/fpga/fpga_parallel.h"
#include "../drivers/fpga/fpga_tile_test.h"
#include "../user/fpga/fpga_parallel_app.h"
#include <m5os/display.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
static unsigned allocations,opens,closes,draws;static int fault;static bool paused;
void *kmalloc_psram(size_t n){void*p=malloc(n);if(p)allocations++;return p;}
void kfree(void*p){if(p){assert(allocations);allocations--;free(p);}}
void kprintf(const char*f,...){va_list a;va_start(a,f);vprintf(f,a);va_end(a);}
int hal_console_getc(void){return 'q';}
void task_sleep(uint32_t n){(void)n;}
void fbcon_pause(bool p){paused=p;}
bool display_begin_frame(void){assert(closes==1&&paused);return fault!=1;}
bool display_end_frame(void){return fault!=2;}
void display_clear(uint16_t c){(void)c;}
void display_draw_bitmap_scaled(int x,int y,int w,int h,const uint16_t*p,int s){(void)x;(void)y;(void)w;(void)h;(void)p;(void)s;draws++;}
int fpga_parallel_open(fpga_parallel_t*b,const char*p,uint32_t hz){(void)p;opens++;b->native=(void*)b;b->stats.frequency_hz=hz;b->stats.rx_frequency_hz=hz;return 0;}
void fpga_parallel_close(fpga_parallel_t*b){closes++;if(fault==3)b->error=FPGA_LINK_IO;}
int hal_parlio_set_rx_frequency(hal_parlio_t*h,uint32_t hz){fpga_parallel_t*b=(void*)h;b->stats.rx_frequency_hz=hz;return 0;}
const fpga_wire_ops_t fpga_parallel_wire_ops={0};
int fpga_wire_init(fpga_wire_t*w,const fpga_wire_ops_t*o,void*c,fpga_wire_workspace_t*s){w->ops=o;w->context=c;w->workspace=s;return 0;}
int fpga_wire_caps(fpga_wire_t*w,uint32_t t){(void)t;w->sync_capable=true;return 0;}
int fpga_wire_tile_caps(fpga_wire_t*w,uint32_t t){(void)t;w->tile_capable=true;return 0;}
int fpga_wire_batch_caps(fpga_wire_t*w,uint32_t t){(void)t;w->batch_capable=true;return 0;}
int fpga_wire_command(fpga_wire_t*w,uint8_t o,uint8_t a,uint8_t b,bool tag,uint8_t*d,size_t n,uint32_t t){(void)w;(void)o;(void)a;(void)b;(void)tag;(void)d;(void)n;(void)t;return 0;}
int fpga_tile_test_run(fpga_wire_t*w,fpga_tile_test_workspace_t*s,fpga_tile_test_report_t*r,bool tag,bool frac,uint32_t t){(void)w;(void)s;(void)tag;(void)frac;(void)t;r->image_valid=true;r->scenes=6;return 0;}
int fpga_tile_batch_test_run(fpga_wire_t*w,fpga_tile_test_workspace_t*s,fpga_tile_test_report_t*r,bool tag,unsigned n,uint32_t t){(void)n;return fpga_tile_test_run(w,s,r,tag,true,t);}
int main(int argc,char**argv){if(argc>1&&!strncmp(argv[1],"fault",5)){fault=atoi(argv[1]+5);argc--;argv++;}int r=fpga_parallel_cmd_main(argc,argv);assert(!allocations&&!paused);assert(opens==closes);if(fault==1||fault==3)assert(!draws);return r;}
