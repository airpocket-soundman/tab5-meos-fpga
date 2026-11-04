#include <m5os/device.h>
#include <m5os/dev_fpga.h>
#include <m5os/app.h>
#include <m5os/errno.h>
#include <m5os/hal_time.h>
#include <m5os/kmalloc.h>
#include <string.h>
#if M5OS_MODULE_FPGA_PARLIO && M5OS_BOARD_TAB5
#include "fpga_parallel.h"
#include "../../third_party/carrier_link/link_protocol.h"

enum { COMMAND_TIMEOUT_US=500000 };
typedef struct {
    fpga_parallel_t bus;
    fpga_wire_t wire;
    fpga_wire_workspace_t workspace;
    uint8_t body[FPGA_WIRE_TILE_BYTES];
} gpu_session_t;
/* device_ioctl holds the device mutex for the entire physical transaction.
 * Retain this kernel allocation on killed-task/unsafe-close paths: a live DMA
 * callback or protected pin lease must never point into reclaimed app memory.
 * An interrupted/quarantined session refuses new work until reboot/recovery. */
static gpu_session_t *active_session;

static int translate_link(int e)
{
    if(!e)return 0;
    if(e==FPGA_LINK_BUSY)return ERR_BUSY;
    if(e==FPGA_LINK_UNSUPPORTED)return ERR_NOSYS;
    if(e==FPGA_LINK_TIMEOUT)return ERR_TIMEOUT;
    if(e==FPGA_LINK_INVALID||e==FPGA_LINK_RANGE)return ERR_INVAL;
    return ERR_IO;
}
static int command(gpu_session_t *s,uint8_t op,uint16_t value,uint16_t *out)
{
    int e=fpga_wire_command(&s->wire,op,(uint8_t)value,(uint8_t)(value>>8),true,s->body,8,COMMAND_TIMEOUT_US);
    if(!e&&out)*out=(uint16_t)(s->body[4]|(uint16_t)s->body[5]<<8);
    return e;
}
static bool valid(const fpga_fractal_request_t *r)
{
    return r&&r->version==FPGA_FRACTAL_ABI_VERSION&&r->size==sizeof *r&&
        r->pixels&&r->pixel_count==FPGA_FRACTAL_PIXELS&&
        ((uintptr_t)r->pixels%_Alignof(uint16_t))==0&&
        (uintptr_t)r->pixels<=UINTPTR_MAX-2u*FPGA_FRACTAL_PIXELS&&
        r->iterations>=1&&r->iterations<=128&&r->mode<=3&&
        r->dx>=1&&r->dx<=32767&&r->dy>=1&&r->dy<=32767&&
        (int32_t)r->x0+31*(int32_t)r->dx<=32767&&
        (int32_t)r->y0+31*(int32_t)r->dy<=32767;
}
static int gpu_ioctl(device_t *dev,uint32_t op,void *arg)
{
    (void)dev;
    if(op!=FPGA_IOC_FRACTAL_RENDER)return ERR_NOSYS;
    if(!task_has_perm(APP_PERM_DISPLAY))return ERR_PERM;
    fpga_fractal_request_t *r=arg;
    if(!valid(r))return ERR_INVAL;
    /* Snapshot immutable inputs before waiting on/using the shared device. */
    fpga_fractal_request_t request=*r;
    r->elapsed_us=0;r->engine_cycles=0;r->cycles_valid=false;
    if(active_session)return ERR_BUSY;
    gpu_session_t *s=kmalloc_psram(sizeof *s);
    if(!s)return ERR_NOMEM;
    memset(s,0,sizeof *s);active_session=s;
    uint64_t began=hal_time_now_us();uint16_t info=0,low=0,high=0;
    /* Conservative initial clock; changing the device's fixed policy needs
     * an explicit verified operating-point update, not an app GPIO command. */
    int e=fpga_parallel_open(&s->bus,"dock-jumper-v0",1000000);
    if(!e)e=fpga_wire_init(&s->wire,&fpga_parallel_wire_ops,&s->bus,&s->workspace);
    if(!e)e=fpga_wire_caps(&s->wire,COMMAND_TIMEOUT_US);
    if(!e)e=fpga_wire_tile_caps(&s->wire,COMMAND_TIMEOUT_US);
    if(!e)s->bus.tagged=true;
    if(!e)e=command(s,FRACTAL_INFO,0,&info);
    if(!e&&((info>>8)<1||(info>>8)>2||(info&3u)!=3u||
             ((info>>8)==2&&(info&12u)!=12u)||
             ((request.mode&2u)&&!(info&16u))))e=FPGA_LINK_UNSUPPORTED;
    if(!e)e=fpga_parallel_continuous(&s->bus);
    if(!e)e=fpga_wire_transport(&s->wire,&fpga_parallel_stream_wire_ops,&s->bus);
    const uint16_t values[]={(uint16_t)request.x0,(uint16_t)request.y0,request.dx,request.dy,
        (uint16_t)request.cx,(uint16_t)request.cy,(uint16_t)(request.iterations|((uint16_t)request.mode<<8))};
    for(unsigned i=0;!e&&i<7;i++)e=command(s,(uint8_t)(FRACTAL_X0+i),values[i],NULL);
    if(!e)e=command(s,FRACTAL_DRAW,0,NULL);
    if(!e&&(info&4u))e=command(s,FRACTAL_CYCLES_LO,0,&low);
    if(!e&&(info&4u))e=command(s,FRACTAL_CYCLES_HI,0,&high);
    if(!e)e=fpga_wire_tile(&s->wire,0,true,s->body,COMMAND_TIMEOUT_US);
    fpga_parallel_close(&s->bus);
    bool released=!s->bus.native&&!s->bus.owner&&!s->bus.owner_guarded&&!s->bus.stream;
    if(!released) {
        /* Keep callbacks, DMA state and protected IO alive. Never return pixels
         * from an operation whose physical close could not be proved safe. */
        return ERR_IO;
    }
    if(!e) {
        for(unsigned i=0;i<FPGA_FRACTAL_PIXELS;i++)request.pixels[i]=
            (uint16_t)(s->body[8+2*i]|(uint16_t)s->body[9+2*i]<<8);
        r->elapsed_us=(uint32_t)(hal_time_now_us()-began);
        r->engine_cycles=(uint32_t)low|((uint32_t)high<<16);
        r->cycles_valid=(info&4u)!=0;
    }
    active_session=NULL;kfree(s);return translate_link(e);
}
static const device_ops_t ops={.ioctl=gpu_ioctl};
static device_t dev={.name="fpga",.ops=&ops,.required_perm=APP_PERM_DISPLAY};
void fpga_dev_register(void){device_register(&dev);}
#endif
