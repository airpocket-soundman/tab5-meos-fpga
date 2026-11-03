#include "fpga_gfx_app.h"
#include "../../drivers/fpga/fpga_gfx.h"
#include "../../drivers/fpga/fpga_gfx_mock.h"
#include "../../drivers/fpga/fpga_gpio.h"
#include <m5os/display.h>
#include <m5os/hal_console.h>
#include <m5os/kmalloc.h>
#include <m5os/kprintf.h>
#include <string.h>

static bool cancel(void *ctx) {(void)ctx;return hal_console_getc()==3;}
static bool number(const char *s,unsigned *out)
{
    unsigned v=0,base=10;
    if(s[0]=='0'&&(s[1]=='x'||s[1]=='X')){s+=2;base=16;}
    if(!*s)return false;
    for(;*s;s++) {
        unsigned d=*s>='0'&&*s<='9'?(unsigned)(*s-'0'):
            *s>='a'&&*s<='f'?(unsigned)(*s-'a'+10):
            *s>='A'&&*s<='F'?(unsigned)(*s-'A'+10):65536;
        if(d>=base||v>(65535-d)/base)return false;
        v=v*base+d;
    }
    *out=v;return true;
}
static int usage(void)
{
    kprintf("fpga --dock dock-jumper-v0 gfx info|test|demo\n"
            "fpga --dock dock-jumper-v0 gfx color|clear RGB565\n"
            "fpga --dock dock-jumper-v0 gfx pixel X Y RGB565\n"
            "fpga --dock dock-jumper-v0 gfx rect X Y W H RGB565\n"
            "fpga --dock dock-jumper-v0 gfx read X Y\n"
            "Use --mock gfx ... for software only. test/demo overwrite the 32x32 image.\n");
    return 1;
}
int fpga_gfx_cmd_main(int argc,char **argv)
{
    bool mock=argc>1&&!strcmp(argv[1],"--mock");
    unsigned start=mock?3:4;
    if(argc<=(int)start||(!mock&&(strcmp(argv[1],"--dock")||strcmp(argv[2],"dock-jumper-v0"))))return usage();
    const char *cmd=argv[start];unsigned n=0,a[5]={0};
    bool info=!strcmp(cmd,"info"),test=!strcmp(cmd,"test"),demo=!strcmp(cmd,"demo");
    bool color=!strcmp(cmd,"color"),clear=!strcmp(cmd,"clear"),pixel=!strcmp(cmd,"pixel");
    bool rect=!strcmp(cmd,"rect"),read=!strcmp(cmd,"read");
    if(color||clear)n=1;else if(pixel)n=3;else if(rect)n=5;else if(read)n=2;
    else if(!info&&!test&&!demo)return usage();
    if(argc!=(int)(start+1+n))return usage();
    for(unsigned i=0;i<n;i++)if(!number(argv[start+1+i],&a[i]))return usage();
    if((pixel||rect||read)&&(a[0]>=32||a[1]>=32))return usage();
    if(rect&&(!a[2]||!a[3]||a[2]>32-a[0]||a[3]>32-a[1]))return usage();

    /* Both 2KiB buffers live on the heap, not the 4KiB shell task stack. */
    fpga_gfx_mock_t *model=mock?kmalloc(sizeof *model):NULL;
    uint16_t *pixels=(test||demo)?kmalloc(FPGA_GFX_PIXELS*sizeof *pixels):NULL;
    if((mock&&!model)||((test||demo)&&!pixels)) {
        kfree(model);kfree(pixels);kprintf("fpga gfx: no memory\n");return 1;
    }
    fpga_mock_t transport;fpga_gpio_t gpio={0};fpga_link_t link={0};
    int error=0;
    if(mock) {
        fpga_mock_init(&transport,1);fpga_gfx_mock_init(model,&transport);
        link.ops=&fpga_mock_ops;link.context=&transport;
    } else {
        error=fpga_gpio_open(&gpio,"dock-jumper-v0");
        gpio.cancel=cancel;
        link.ops=&fpga_gpio_ops;link.context=&gpio;
    }
    unsigned compared=0;uint16_t value=0;bool shown=false;
    if(!error) {
        if(info)error=fpga_gfx_info(&link);
        else if(color)error=fpga_gfx_color(&link,(uint16_t)a[0]);
        else if(clear)error=fpga_gfx_clear(&link,(uint16_t)a[0]);
        else if(pixel)error=fpga_gfx_pixel(&link,a[0],a[1],(uint16_t)a[2]);
        else if(rect)error=fpga_gfx_rect(&link,a[0],a[1],a[2],a[3],(uint16_t)a[4]);
        else if(read)error=fpga_gfx_read(&link,a[0],a[1],&value);
        else error=fpga_gfx_diagnostic(&link,pixels,&compared);
    }
    if(!mock) {
        if(gpio.error)error=gpio.error;
        fpga_gpio_close(&gpio);
    }
    bool reset_required=!mock&&link.attempted&&error;
    bool readback_pass=!error&&(test||demo)&&compared==FPGA_GFX_PIXELS;
    if(!error&&demo) {
        int scale=16;
        while(scale>1&&(32*scale>DISPLAY_WIDTH||32*scale>DISPLAY_HEIGHT))scale--;
        bool batch=display_begin_frame();
        display_draw_bitmap_scaled((DISPLAY_WIDTH-32*scale)/2,(DISPLAY_HEIGHT-32*scale)/2,32,32,pixels,scale);
        shown=!batch||display_end_frame();
        if(!shown)error=FPGA_LINK_IO;
    }
    kprintf("{\"backend\":\"%s\",\"protocol\":\"dock-gfx-v0-experimental\","
            "\"result\":\"%s\",\"transactions\":%u,\"width\":32,\"height\":32,"
            "\"pixel\":%u,\"pixels_compared\":%u,\"readback_pass\":%s,\"lcd_submitted\":%s,\"reset_required\":%s}\n",
            mock?"mock":"dock-gpio",fpga_link_error(error),link.completed,value,compared,
            readback_pass?"true":"false",shown?"true":"false",reset_required?"true":"false");
    kfree(pixels);kfree(model);return error?1:0;
}
