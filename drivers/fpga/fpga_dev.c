#include <m5os/device.h>
#include <m5os/dev_fpga.h>
#include <m5os/app.h>
#include <m5os/task.h>
#include <m5os/arch.h>
#include <m5os/errno.h>
#include <m5os/hal_time.h>
#include <m5os/kmalloc.h>
#include <m5os/kprintf.h>
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
    task_t *owner;
    uint32_t owner_id;
    uint16_t info;
    bool held,poisoned,config_ready;
    uint16_t settings[7];
} gpu_session_t;
/* Serialized by device_ioctl. Unsafe close retains DMA/IRQ memory and pins. */
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
static bool owned(gpu_session_t*s)
{
    task_t*t=task_current();return t&&s->owner==t&&s->owner_id==t->id;
}
static bool owner_dead(gpu_session_t*s)
{
    return !s->owner||s->owner->id!=s->owner_id||s->owner->state==TASK_UNUSED||s->owner->state==TASK_TERMINATED;
}
static bool close_bus(gpu_session_t*s)
{
    fpga_parallel_close(&s->bus);
    bool safe=!s->bus.native&&!s->bus.owner&&!s->bus.owner_guarded&&!s->bus.stream;
    if(!safe)s->poisoned=true;
    return safe;
}
static void discard(gpu_session_t*s){active_session=NULL;kfree(s);}
static void failure(gpu_session_t*s,int e)
{
    const fpga_wire_failure_t*f=&s->workspace.failure;
    kprintf("{\"fpga_device_failure\":true,\"link_error\":%d,\"op\":%u,\"stage\":\"%s\",\"max_gap_us\":%u,\"stream_reason\":%u,\"stream_fatal\":%s,\"raw\":[",
        e,f->valid?f->request_header[1]:0,s->bus.stats.stage?s->bus.stats.stage:"none",
        s->bus.max_gap_us,s->bus.stream_stats.stop_reason,s->bus.stream_stats.fatal?"true":"false");
    const uint8_t*raw=f->saved_length?f->raw:s->bus.short_failure;
    size_t n=f->saved_length?f->saved_length:s->bus.short_failure_length;
    for(unsigned i=0;f->valid&&i<n&&i<24;i++)kprintf("%s%u",i?",":"",raw[i]);
    kprintf("]}\n");
}
static int command(gpu_session_t*s,uint8_t op,uint16_t value,uint16_t*out)
{
    int e=fpga_wire_command(&s->wire,op,(uint8_t)value,(uint8_t)(value>>8),true,s->body,8,COMMAND_TIMEOUT_US);
    if(!e&&out)*out=(uint16_t)(s->body[4]|(uint16_t)s->body[5]<<8);
    return e;
}
static int begin_session(bool held)
{
    if(active_session)return ERR_BUSY;
    gpu_session_t*s=kmalloc_psram(sizeof*s);if(!s)return ERR_NOMEM;
    memset(s,0,sizeof*s);active_session=s;s->held=held;s->owner=task_current();
    s->owner_id=s->owner?s->owner->id:0;
    /* Fixed GPIO control timing plus 5MHz bulk RX. The 1MHz setting is
     * retained for native DMA/long requests, not the GPIO control bit rate. */
    int e=fpga_parallel_open(&s->bus,"dock-jumper-v0",1000000);
    if(!e&&hal_parlio_set_rx_frequency(s->bus.native,5000000))e=FPGA_LINK_IO;
    s->bus.fast_control=true;
    if(!e)e=fpga_wire_init(&s->wire,&fpga_parallel_wire_ops,&s->bus,&s->workspace);
    if(!e)e=fpga_wire_caps(&s->wire,COMMAND_TIMEOUT_US);
    if(!e)e=fpga_wire_tile_caps(&s->wire,COMMAND_TIMEOUT_US);
    if(!e)s->bus.tagged=true;
    if(!e)e=command(s,FRACTAL_INFO,0,&s->info);
    if(!e&&((s->info>>8)<1||(s->info>>8)>2||(s->info&3u)!=3u||
             ((s->info>>8)==2&&(s->info&12u)!=12u)))e=FPGA_LINK_UNSUPPORTED;
    if(!e)e=fpga_parallel_continuous(&s->bus);
    if(!e)e=fpga_wire_transport(&s->wire,&fpga_parallel_stream_wire_ops,&s->bus);
    if(e){bool safe=close_bus(s);failure(s,e);if(safe)discard(s);return safe?translate_link(e):ERR_IO;}
    return 0;
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
static int gpu_close(device_t*dev)
{
    (void)dev;gpu_session_t*s=active_session;
    if(!s||(!owned(s)&&!owner_dead(s)))return 0;
    if(!close_bus(s))return ERR_IO;
    discard(s);return 0;
}
static int gpu_ioctl_impl(device_t*dev,uint32_t op,void*arg)
{
    (void)dev;
    if(op!=FPGA_IOC_FRACTAL_RENDER&&op!=FPGA_IOC_FRACTAL_BEGIN&&op!=FPGA_IOC_FRACTAL_END)return ERR_NOSYS;
    if(!task_has_perm(APP_PERM_DISPLAY))return ERR_PERM;
    if(op==FPGA_IOC_FRACTAL_BEGIN)return arg?ERR_INVAL:begin_session(true);
    if(op==FPGA_IOC_FRACTAL_END){
        if(arg)return ERR_INVAL;
        if(active_session&&!owned(active_session))return ERR_BUSY;
        return gpu_close(dev);
    }
    fpga_fractal_request_t*r=arg;
    if(!valid(r))return ERR_INVAL;
    fpga_fractal_request_t request=*r;
    r->elapsed_us=0;r->engine_cycles=0;r->cycles_valid=false;
    if(active_session&&(!active_session->held||!owned(active_session)||active_session->poisoned))return ERR_BUSY;
    uint64_t began=hal_time_now_us();
    if(!active_session){int error=begin_session(false);if(error)return error;}
    gpu_session_t*s=active_session;
    int e=((request.mode&2u)&&!(s->info&16u))?FPGA_LINK_UNSUPPORTED:0;
    uint16_t low=0,high=0;
    const uint16_t values[]={(uint16_t)request.x0,(uint16_t)request.y0,request.dx,request.dy,
        (uint16_t)request.cx,(uint16_t)request.cy,(uint16_t)(request.iterations|((uint16_t)request.mode<<8))};
    /* Carrier/ESP-IDF tile path retains DX/DY/CX/CY/CONFIG between DRAWs.
     * Always set tile origin; skip only identical shared setters in this
     * exclusively owned live session. Errors discard the entire cache. */
    for(unsigned i=0;!e&&i<7;i++){
        if(i>=2&&s->config_ready&&s->settings[i]==values[i])continue;
        e=command(s,(uint8_t)(FRACTAL_X0+i),values[i],NULL);
        if(!e)s->settings[i]=values[i];
    }
    if(!e)s->config_ready=true;
    if(!e)e=command(s,FRACTAL_DRAW,0,NULL);
    if(!e&&(s->info&4u))e=command(s,FRACTAL_CYCLES_LO,0,&low);
    if(!e&&(s->info&4u))e=command(s,FRACTAL_CYCLES_HI,0,&high);
    if(!e)e=fpga_wire_tile(&s->wire,0,true,s->body,COMMAND_TIMEOUT_US);
    bool finish=e||!s->held;
    if(finish&&!close_bus(s))return ERR_IO;
    if(e)failure(s,e);
    if(!e){
        for(unsigned i=0;i<FPGA_FRACTAL_PIXELS;i++)request.pixels[i]=(uint16_t)(s->body[8+2*i]|(uint16_t)s->body[9+2*i]<<8);
        r->elapsed_us=(uint32_t)(hal_time_now_us()-began);
        r->engine_cycles=(uint32_t)low|((uint32_t)high<<16);r->cycles_valid=(s->info&4u)!=0;
    }
    if(finish)discard(s);
    return translate_link(e);
}
/* The carrier abandons a partial request/reply after 100ms without clocks.
 * MeOS native apps run below UI services: measured preemption was 148ms,
 * corrupting otherwise valid exchanges. Keep this bounded synchronous device
 * operation ahead of ordinary tasks, with DMA/LCD/USB interrupts still enabled.
 * Restore the exact priority on every returned path, including errors. */
static int gpu_ioctl(device_t*dev,uint32_t op,void*arg)
{
    if(op!=FPGA_IOC_FRACTAL_RENDER&&op!=FPGA_IOC_FRACTAL_BEGIN&&op!=FPGA_IOC_FRACTAL_END)return ERR_NOSYS;
    if(!task_has_perm(APP_PERM_DISPLAY))return ERR_PERM;
    task_t*self=task_current();if(!self)return ERR_INVAL;
    uint32_t irq=arch_irq_disable();uint8_t saved=self->priority;
    self->priority=TASK_PRIO_MAX;arch_irq_restore(irq);
    int error=gpu_ioctl_impl(dev,op,arg);
    irq=arch_irq_disable();self->priority=saved;arch_irq_restore(irq);
    return error;
}
static const device_ops_t ops={.close=gpu_close,.ioctl=gpu_ioctl};
static device_t dev={.name="fpga",.ops=&ops,.required_perm=APP_PERM_DISPLAY};
void fpga_dev_register(void){device_register(&dev);}
#endif
