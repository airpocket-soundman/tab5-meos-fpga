#include "fpga_surface.h"
#include "../../third_party/carrier_link/link_protocol.h"
#include <m5os/display.h>
#include <string.h>

static bool overlaps(const void *a,size_t n,const void *b,size_t m)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return n>UINTPTR_MAX-x||m>UINTPTR_MAX-y||(x<y+m&&y<x+n);
}
static bool valid(const fpga_surface_t *s)
{
    return s&&s->wire&&s->wire->ops&&s->wire->ops->now_us&&s->workspace;
}
static int checkpoint(fpga_surface_t *s,uint64_t end)
{
    if(s->wire->ops->cancelled&&s->wire->ops->cancelled(s->wire->context))return FPGA_LINK_CANCELLED;
    return s->wire->ops->now_us(s->wire->context)>=end?FPGA_LINK_TIMEOUT:0;
}
static uint32_t budget(fpga_surface_t *s,uint64_t end)
{
    uint64_t now=s->wire->ops->now_us(s->wire->context);
    if(now>=end)return 0;
    uint64_t left=end-now;
    return left>FPGA_LINK_TIMEOUT_MAX_US?FPGA_LINK_TIMEOUT_MAX_US:(uint32_t)left;
}
static int start(fpga_surface_t *s,uint32_t timeout,uint64_t *end)
{
    if(!valid(s)||!timeout||timeout>FPGA_SURFACE_TIMEOUT_MAX_US)return FPGA_LINK_INVALID;
    if(s->busy||s->wire->busy)return FPGA_LINK_BUSY;
    if(s->poisoned)return FPGA_LINK_IO;
    uint64_t now=s->wire->ops->now_us(s->wire->context);
    if(now>UINT64_MAX-timeout)return FPGA_LINK_INVALID;
    *end=now+timeout;s->busy=true;return 0;
}
static int done(fpga_surface_t *s,int error)
{
    s->busy=false;s->last_error=error;
    if(error){s->poisoned=true;s->active=false;s->image_valid=false;s->presented=false;}
    return error;
}
static int rpc(fpga_surface_t *s,uint8_t op,uint16_t value,uint64_t end)
{
    int error=checkpoint(s,end);if(error)return error;
    uint32_t left=budget(s,end);if(!left)return FPGA_LINK_TIMEOUT;
    return fpga_wire_command(s->wire,op,(uint8_t)value,(uint8_t)(value>>8),s->tagged,s->workspace->body,8,left);
}
static int flush(fpga_surface_t *s,uint64_t end)
{
    int error=checkpoint(s,end);if(error||!s->queued)return error;
    uint32_t left=budget(s,end);if(!left)return FPGA_LINK_TIMEOUT;
    error=fpga_wire_batch(s->wire,s->workspace->records,s->queued,s->tagged,s->workspace->body,left);
    if(!error){s->triangles+=s->queued;s->batches++;s->queued=0;}
    return error;
}
int fpga_surface_init(fpga_surface_t *s,fpga_wire_t *wire,fpga_surface_workspace_t *work,bool tagged)
{
    if(!s||!wire||!wire->ops||!wire->ops->now_us||!wire->workspace||!work)return FPGA_LINK_INVALID;
    if(wire->busy)return FPGA_LINK_BUSY;
    if(tagged&&!wire->sync_capable)return FPGA_LINK_UNSUPPORTED;
    if(overlaps(s,sizeof *s,work,sizeof *work)||overlaps(s,sizeof *s,wire,sizeof *wire)||
       overlaps(s,sizeof *s,wire->workspace,sizeof *wire->workspace)||
       overlaps(work,sizeof *work,wire,sizeof *wire)||overlaps(work,sizeof *work,wire->workspace,sizeof *wire->workspace))return FPGA_LINK_INVALID;
    memset(s,0,sizeof *s);s->wire=wire;s->workspace=work;s->tagged=tagged;return 0;
}
int fpga_surface_begin(fpga_surface_t *s,uint16_t background,uint32_t timeout)
{
    if(!valid(s))return FPGA_LINK_INVALID;
    if(s->active||s->generation==UINT64_MAX)return s->active?FPGA_LINK_BUSY:FPGA_LINK_RANGE;
    uint64_t end;int error=start(s,timeout,&end);if(error)return error;
    s->image_valid=false;s->presented=false;s->queued=0;s->generation++;
    if(!s->wire->tile_capable){error=rpc(s,TRI_TILE_CAPS,0,end);if(!error){if(s->workspace->body[4]!=3||s->workspace->body[5]!=1)error=FPGA_LINK_UNSUPPORTED;else s->wire->tile_capable=true;}}
    if(!error&&!s->wire->batch_capable){error=rpc(s,TRI_BATCH_CAPS,0,end);if(!error){if(s->workspace->body[4]!=64||s->workspace->body[5]!=1)error=FPGA_LINK_UNSUPPORTED;else s->wire->batch_capable=true;}}
    if(!error){error=rpc(s,TRI_INFO,0,end);if(!error&&(s->workspace->body[4]!=32||s->workspace->body[5]!=32))error=FPGA_LINK_UNSUPPORTED;}
    if(!error)error=rpc(s,TRI_CLEAR,background,end);
    if(!error)error=checkpoint(s,end);
    if(!error)s->active=true;
    return done(s,error);
}
int fpga_surface_queue(fpga_surface_t *s,const fpga_surface_triangle_t *v)
{
    if(!valid(s)||!v)return FPGA_LINK_INVALID;
    if(s->busy||s->wire->busy)return FPGA_LINK_BUSY;
    if(s->poisoned)return FPGA_LINK_IO;
    if(!s->active)return FPGA_LINK_UNINITIALIZED;
    if(s->queued>=FPGA_SURFACE_TRIANGLES_MAX)return FPGA_LINK_BUSY;
    fpga_surface_triangle_t copy=*v;v=&copy;
    uint16_t *r=s->workspace->records+10*s->queued;
    r[0]=(uint16_t)v->ax;r[1]=(uint16_t)v->ay;r[2]=(uint16_t)v->bx;r[3]=(uint16_t)v->by;
    r[4]=(uint16_t)v->cx;r[5]=(uint16_t)v->cy;r[6]=v->za;r[7]=v->zb;r[8]=v->zc;r[9]=v->color;
    s->queued++;return 0;
}
int fpga_surface_flush(fpga_surface_t *s,uint32_t timeout)
{
    if(!valid(s))return FPGA_LINK_INVALID;
    if(s->busy||s->wire->busy)return FPGA_LINK_BUSY;
    if(!s->active)return s->poisoned?FPGA_LINK_IO:FPGA_LINK_UNINITIALIZED;
    uint64_t end;int error=start(s,timeout,&end);if(error)return error;
    error=flush(s,end);if(!error)error=checkpoint(s,end);
    return done(s,error);
}
int fpga_surface_finish(fpga_surface_t *s,uint32_t timeout)
{
    if(!valid(s))return FPGA_LINK_INVALID;
    if(s->busy||s->wire->busy)return FPGA_LINK_BUSY;
    if(!s->active)return s->poisoned?FPGA_LINK_IO:FPGA_LINK_UNINITIALIZED;
    uint64_t end;int error=start(s,timeout,&end);if(error)return error;
    error=flush(s,end);
    if(!error){uint32_t left=budget(s,end);error=left?fpga_wire_tile(s->wire,0,s->tagged,s->workspace->body,left):FPGA_LINK_TIMEOUT;}
    if(!error)for(unsigned i=0;i<FPGA_SURFACE_PIXELS;i++){
        if(!(i%32)){error=checkpoint(s,end);if(error)break;}
        s->workspace->color[i]=(uint16_t)(s->workspace->body[8+2*i]|(uint16_t)s->workspace->body[9+2*i]<<8);
    }
    if(!error)error=checkpoint(s,end);
    if(!error){s->readbacks++;s->image_generation=s->generation;s->image_valid=true;s->active=false;}
    return done(s,error);
}
const uint16_t *fpga_surface_image(const fpga_surface_t *s,uint64_t *generation)
{
    if(!s||!s->workspace||s->busy||s->poisoned||!s->image_valid)return NULL;
    if(generation)*generation=s->image_generation;
    return s->workspace->color;
}
int fpga_surface_present(fpga_surface_t *s,int x,int y,unsigned scale)
{
    if(!s||!s->workspace)return FPGA_LINK_INVALID;
    if(s->busy)return FPGA_LINK_BUSY;
    if(!s->image_valid||s->poisoned)return FPGA_LINK_UNINITIALIZED;
    if(x<0||y<0||!scale||scale>DISPLAY_WIDTH/32u||scale>DISPLAY_HEIGHT/32u||
       (unsigned)x>DISPLAY_WIDTH-32u*scale||(unsigned)y>DISPLAY_HEIGHT-32u*scale)return FPGA_LINK_RANGE;
    s->busy=true;s->presented=false;
    int error=0;
    if(!display_begin_frame())error=FPGA_LINK_BUSY;
    else {
        display_draw_bitmap_scaled(x,y,32,32,s->workspace->color,(int)scale);
        if(!display_end_frame())error=FPGA_LINK_IO;
    }
    if(!error){s->presentations++;s->presented=true;}
    s->busy=false;s->last_error=error;return error;
}
