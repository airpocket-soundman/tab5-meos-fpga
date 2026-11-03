/* GPIO-level device model: checks bus ownership, setup time and turnaround. */
#include "../drivers/fpga/fpga_gpio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#if M5OS_MODULE_FPGA_GFX
#include "../drivers/fpga/fpga_gfx_mock.h"
#include <stdlib.h>
static fpga_gfx_mock_t gfx;
static fpga_mock_t device;
static unsigned allocations,display_calls;
static bool bad_read,unsupported,fail_alloc,fail_lcd,cancel_gfx;
void *kmalloc(size_t n) {if(fail_alloc)return NULL;void *p=malloc(n);if(p)allocations++;return p;}
void kfree(void *p) {if(p){assert(allocations);allocations--;free(p);}}
bool display_begin_frame(void) {return true;}
bool display_end_frame(void) {return !fail_lcd;}
void display_draw_bitmap_scaled(int x,int y,int w,int h,const uint16_t *pixels,int scale)
{
    (void)x;(void)y;assert(w==32&&h==32&&scale>0);
    assert(pixels[0]==0x0841&&pixels[2*32+1]==0xf800&&pixels[31]==0x001f);
    assert(pixels[31*32]==0xffe0&&pixels[29*32+30]==0xf81f);display_calls++;
}
#endif
#include "../user/fpga/fpga_app.h"
static const int dp[]={18,19,5,3,4,2,48,47};
static bool output[55],claimed,conflict,no_reply,corrupt,cancel_flag,stalled;
static int level[55],ready,rx,tx;
static unsigned clocks;
static uint64_t clock_us,last_edge,last_data,release_time,dir_time;
static uint8_t request[8],reply[8];
void kprintf(const char *format,...) {va_list a;va_start(a,format);vprintf(format,a);va_end(a);}
int hal_console_getc(void)
{
#if M5OS_MODULE_FPGA_GFX
    if(cancel_gfx&&request[1]==FPGA_GFX_READ&&rx>=4)return 3;
#endif
    return -1;
}
int kconsole_getc(void) {return hal_console_getc();}
uint64_t hal_time_now_us(void) {return clock_us;}
void hal_delay_us(uint32_t us) {if(!stalled)clock_us+=us;}
int io_owner_create(const char *n,io_owner_t *o) {(void)n;*o=1;return 0;}
int io_owner_close(io_owner_t o) {(void)o;memset(output,0,sizeof output);claimed=false;return 0;}
int io_pins_claim(io_owner_t o,const int *p,size_t n,io_use_t u)
{(void)o;(void)p;assert(n==15&&u==IO_USE_DIGITAL);if(conflict)return -1;claimed=true;return 0;}
static bool data_outputs(void) {for(unsigned i=0;i<8;i++)if(output[dp[i]])return true;return false;}
int io_gpio_config(io_owner_t o,int p,bool out,int initial,io_pull_t pull)
{
    (void)o;(void)pull;assert(claimed);
    if(p==45||p==17||p==52||p==6||p==7)assert(!out);
    for(unsigned i=0;i<8;i++)if(p==dp[i]) {
        if(out)assert(!level[51]&&clock_us-dir_time>=200);
        if(output[p]&&!out)release_time=clock_us;
    }
    output[p]=out;level[p]=initial;return 0;
}
int io_gpio_write(io_owner_t o,int p,int v)
{
    (void)o;if(!output[p])return -1;
    for(unsigned i=0;i<8;i++)if(p==dp[i])last_data=clock_us;
    if(p==51&&v!=level[p]) {
        assert(!data_outputs());
        if(v)assert(clock_us-release_time>=200);
        dir_time=clock_us;
    }
    if(p==16&&v!=level[p]) {
        /* Abort cleanup may immediately lower CLK; that partial request must
         * be reset, never treated as a completed normal transfer. */
#if M5OS_MODULE_FPGA_GFX
        bool abort_edge=cancel_gfx&&request[1]==FPGA_GFX_READ&&rx>=4&&!v;
#else
        bool abort_edge=false;
#endif
        if(clocks&&!abort_edge)assert(clock_us-last_edge>=100);
        last_edge=clock_us;
        if(v) {
            clocks++;
            if(!level[51]) {
                assert(clock_us-last_data>=100&&data_outputs()&&!ready);
                unsigned byte=0;for(unsigned i=0;i<8;i++)byte|=(unsigned)level[dp[i]]<<i;
                request[rx++]=(uint8_t)byte;
                if(rx==8) {
#if M5OS_MODULE_FPGA_GFX
                    if(unsupported)fpga_mock_respond(request,reply);
                    else device.respond(&gfx,request,reply);
                    if(bad_read&&request[1]==FPGA_GFX_READ) {
                        reply[4]^=1;uint16_t c=fpga_link_crc16(reply,6);
                        reply[6]=(uint8_t)c;reply[7]=(uint8_t)(c>>8);
                    }
#else
                    reply[0]=0x5a;reply[1]=0;reply[2]=request[2];reply[3]=2;
                    reply[4]=request[1]==1?'T':request[4]^(request[1]==3?255:0);
                    reply[5]=request[1]==1?'P':request[5]^(request[1]==3?255:0);
                    uint16_t c=fpga_link_crc16(reply,6);reply[6]=(uint8_t)c;reply[7]=(uint8_t)(c>>8);
#endif
                    if(corrupt)reply[6]^=1;
                    ready=!no_reply;rx=0;tx=0;
                }
            } else {assert(!data_outputs()&&ready);if(++tx==8)ready=0;}
        }
    }
    level[p]=v;return 0;
}
int io_gpio_read(io_owner_t o,int p,int *v)
{
    (void)o;assert(!output[p]);
    if(p==45) {*v=ready;return 0;}
    assert(level[51]&&clock_us-dir_time>=200&&tx<8);
    for(unsigned i=0;i<8;i++)if(p==dp[i]) {*v=(reply[tx]>>i)&1;return 0;}
    return -1;
}
static bool cancel(void *p) {(void)p;return cancel_flag || clock_us>=1800;}
static void reset(void)
{
#if M5OS_MODULE_FPGA_GFX
    fpga_mock_init(&device,8);fpga_gfx_mock_init(&gfx,&device);
#endif
    memset(output,0,sizeof output);memset(level,0,sizeof level);
    ready=rx=tx=0;clocks=0;clock_us=1000;dir_time=0;no_reply=corrupt=cancel_flag=stalled=false;
}
static void inputs(void) {for(unsigned i=0;i<55;i++)assert(!output[i]);}
int main(int argc,char **argv)
{
    fpga_gpio_t g={0};uint8_t r[2];reset();
#if !M5OS_FPGA_DOCK_VERIFIED
    assert(fpga_gpio_open(&g,"dock-jumper-v0")==FPGA_LINK_INVALID);
    assert(!claimed&&!clocks);inputs();
    puts("Unverified Dock profile rejects before IO activation");return 0;
#endif
    if(argc>1) {
#if M5OS_MODULE_FPGA_GFX
        if(!strcmp(argv[1],"--vectors")) {
            unsigned v[8];
            while(scanf("%2x%2x%2x%2x%2x%2x%2x%2x",&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7])==8) {
                uint8_t q[8],r[8];for(unsigned i=0;i<8;i++)q[i]=(uint8_t)v[i];
                device.respond(&gfx,q,r);for(unsigned i=0;i<8;i++)printf("%02x",r[i]);puts("");
            }
            return 0;
        }
        if(!strcmp(argv[1],"--bad-read")){bad_read=true;argc--;argv++;}
        else if(!strcmp(argv[1],"--unsupported")){unsupported=true;argc--;argv++;}
        else if(!strcmp(argv[1],"--fail-alloc")){fail_alloc=true;argc--;argv++;}
        else if(!strcmp(argv[1],"--fail-lcd")){fail_lcd=true;argc--;argv++;}
        else if(!strcmp(argv[1],"--cancel-gfx")){cancel_gfx=true;argc--;argv++;}
        else if(!strcmp(argv[1],"--no-reply")){no_reply=true;argc--;argv++;}
        else if(!strcmp(argv[1],"--bad-crc")){corrupt=true;argc--;argv++;}
#endif
        int rc=fpga_cmd_main(argc-1,argv+1);inputs();assert(!claimed);
#if M5OS_MODULE_FPGA_GFX
        assert(!allocations);
        if(bad_read||unsupported||cancel_gfx||no_reply||corrupt)assert(!display_calls);
#endif
        return rc;
    }
    assert(fpga_gpio_open(&g,"carrier")==FPGA_LINK_INVALID&&!claimed);
    conflict=true;assert(fpga_gpio_open(&g,"dock-jumper-v0")==FPGA_LINK_BUSY);inputs();conflict=false;
    assert(!fpga_gpio_open(&g,"dock-jumper-v0"));inputs();
    assert(fpga_gpio_open(&g,"dock-jumper-v0")==FPGA_LINK_BUSY&&claimed);
    fpga_link_t l={.ops=&fpga_gpio_ops,.context=&g};
    assert(!fpga_link_exchange(&l,1,0,0,r,100000));
    for(unsigned i=0;i<256;i++)for(unsigned op=2;op<=3;op++) {
        assert(!fpga_link_exchange(&l,(uint8_t)op,(uint8_t)i,(uint8_t)(i^255),r,100000));inputs();
    }
    assert(l.completed==513);fpga_gpio_close(&g);assert(!claimed);inputs();
#if M5OS_MODULE_FPGA_GFX
    for(unsigned chunk=1;chunk<=8;chunk++) {
        fpga_mock_t m;fpga_gfx_mock_t model;uint16_t pixels[1024],value=0xbeef;unsigned compared;
        fpga_mock_init(&m,chunk);fpga_gfx_mock_init(&model,&m);
        fpga_link_t gl={.ops=&fpga_mock_ops,.context=&m};
        assert(fpga_gfx_read(&gl,0,0,&value)==FPGA_LINK_UNINITIALIZED&&value==0xbeef);
        uint32_t before=gl.attempted;
        assert(fpga_gfx_rect(&gl,31,31,2,1,0xffff)==FPGA_LINK_RANGE&&gl.attempted==before);
        assert(fpga_gfx_read(&gl,32,0,&value)==FPGA_LINK_RANGE&&gl.attempted==before);
        assert(!fpga_gfx_diagnostic(&gl,pixels,&compared)&&compared==1024);
        assert(!fpga_gfx_read(&gl,31,0,&value)&&value==0x001f);
        for(unsigned fault=FPGA_MOCK_BAD_CRC;fault<=FPGA_MOCK_WRONG_DATA;fault++) {
            m.fault=fault;value=0xbeef;
            int e=fpga_gfx_read(&gl,0,0,&value);
            if(fault==FPGA_MOCK_BAD_CRC)assert(e==FPGA_LINK_CRC&&value==0xbeef);
            if(fault==FPGA_MOCK_OLD_SEQUENCE)assert(e==FPGA_LINK_SEQUENCE&&value==0xbeef);
        }
        m.fault=FPGA_MOCK_NORMAL;
        fpga_gfx_mock_init(&model,&m); /* device reset loses initialization */
        assert(fpga_gfx_read(&gl,0,0,&value)==FPGA_LINK_UNINITIALIZED);
        m.respond=NULL;
        assert(fpga_gfx_info(&gl)==FPGA_LINK_UNSUPPORTED);
    }
    puts("GFX: all-pixel compare over chunks 1..8; range, uninitialized/reset, CRC, sequence, unsupported passed");
#endif
    for(unsigned fault=0;fault<6;fault++) {
        reset();assert(!fpga_gpio_open(&g,"dock-jumper-v0"));l=(fpga_link_t){.ops=&fpga_gpio_ops,.context=&g};
        if(fault==0)no_reply=true;
        if(fault==1)corrupt=true;
        if(fault==2){g.cancel=cancel;cancel_flag=true;}
        if(fault==3){ready=1;stalled=true;}
        if(fault==5)g.cancel=cancel;
        uint32_t timeout=fault==4?100:100000;
        assert(fpga_link_exchange(&l,1,0,0,r,timeout));inputs();
        if(g.failed) {
            unsigned before=clocks;
            cancel_flag=false;
            assert(fpga_link_exchange(&l,1,0,0,r,100000));
            assert(clocks==before);inputs();
        }
        fpga_gpio_close(&g);assert(!claimed);inputs();
    }
    puts("GPIO: 513 exchanges; contention/setup/turnaround assertions; timeout, CRC, cancellation, stalled timer, short deadline passed");
    return 0;
}
