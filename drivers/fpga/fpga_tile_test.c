#include "fpga_tile_test.h"
#include "../../third_party/carrier_link/link_protocol.h"
#include "../../third_party/carrier_link/fractal_reference.h"
#include <string.h>

typedef struct {
    fpga_wire_t *wire;fpga_tile_test_workspace_t *work;fpga_tile_test_report_t *report;
    uint64_t deadline;bool tagged;
} test_t;
static const int16_t first_triangle[6]={16,32,400,32,16,432};
static const int16_t near_triangle[6]={112,16,496,304,48,480};
static const fractal_config fractals[2]={
    {-10035,-4096,280,264,-2949,819,64,0},
    {-4096,-2048,132,132,-2949,819,64,1}
};
static int check(test_t *t)
{
    if(t->wire->ops->cancelled&&t->wire->ops->cancelled(t->wire->context))return FPGA_LINK_CANCELLED;
    return t->wire->ops->now_us(t->wire->context)>=t->deadline?FPGA_LINK_TIMEOUT:0;
}
static uint32_t remaining(test_t *t)
{
    uint64_t now=t->wire->ops->now_us(t->wire->context);
    if(now>=t->deadline)return 0;
    uint64_t time=t->deadline-now;
    return time>FPGA_LINK_TIMEOUT_MAX_US?FPGA_LINK_TIMEOUT_MAX_US:(uint32_t)time;
}
static int rpc(test_t *t,uint8_t op,uint16_t value,uint16_t *returned)
{
    int error=check(t);if(error)return error;
    uint32_t budget=remaining(t);if(!budget)return FPGA_LINK_TIMEOUT;
    error=fpga_wire_command(t->wire,op,(uint8_t)value,(uint8_t)(value>>8),t->tagged,t->work->body,8,budget);
    if(!error&&returned)*returned=(uint16_t)(t->work->body[4]|(uint16_t)t->work->body[5]<<8);
    return error;
}
static int64_t edge(int32_t ax,int32_t ay,int32_t bx,int32_t by,int32_t px,int32_t py)
{return (int64_t)(bx-ax)*(py-ay)-(int64_t)(by-ay)*(px-ax);}
static bool inclusive(int32_t ax,int32_t ay,int32_t bx,int32_t by)
{return by<ay||(by==ay&&bx>ax);}
static bool covered(const int16_t v[6],unsigned x,unsigned y)
{
    /* Both fixed scenes have positive orientation. Pixel centers/top-left are
     * evaluated exactly in signed Q12.4, with no floating-point approximation. */
    int32_t px=(int32_t)x*16+8,py=(int32_t)y*16+8;
    for(unsigned i=0;i<3;i++){
        unsigned j=(i+1)%3;
        int64_t e=edge(v[2*i],v[2*i+1],v[2*j],v[2*j+1],px,py);
        if(e<0||(!e&&!inclusive(v[2*i],v[2*i+1],v[2*j],v[2*j+1])))return false;
    }
    return true;
}
static unsigned coverage_count(const int16_t v[6])
{
    unsigned count=0;for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++)if(covered(v,x,y))count++;
    return count;
}
static int triangle(test_t *t,const int16_t v[6],uint16_t z,uint16_t color)
{
    for(unsigned i=0;i<6;i++){int error=rpc(t,(uint8_t)(TRI_AX+i),(uint16_t)v[i],NULL);if(error)return error;}
    for(unsigned i=0;i<3;i++){int error=rpc(t,(uint8_t)(TRI_ZA+i),z,NULL);if(error)return error;}
    int error=rpc(t,TRI_COLOR,color,NULL);if(error)return error;
    uint16_t written=0;error=rpc(t,TRI_DRAW,0,&written);
    if(!error&&written!=coverage_count(v))error=FPGA_LINK_DATA;
    return error;
}
static uint16_t expected_pixel(unsigned scene,unsigned plane,unsigned x,unsigned y)
{
    if(scene>=4)return plane?0:fractal_reference_pixel(&fractals[scene-4],x,y,NULL);
    if(scene==3&&covered(near_triangle,x,y))return plane?0x2345:0x001f;
    if(scene>=1&&covered(first_triangle,x,y))return plane?0x1234:0xf800;
    return plane?0:0x0841;
}
static uint16_t batch_expected(unsigned scene,unsigned plane,unsigned x,unsigned y,unsigned count)
{
    unsigned cell=(y/4)*8+x/4;
    /* Right triangle in each4x4 cell: pixel centers on the descending diagonal
     * are excluded by top-left. Thus exactly localx+localy<3 is covered. */
    if(cell>=count||x%4+y%4>=3)return plane?0:0x0841;
    uint16_t depth=(uint16_t)(0x1000+cell*257u);
    uint16_t color=(uint16_t)((cell*977u)^0x5a3cu);
    if(scene==2&&!(cell&1)){depth=(uint16_t)(depth+1024);color^=0xffff;}
    return plane?depth:color;
}
static int compare(test_t *t,unsigned scene,bool batch,unsigned count)
{
    t->report->scene=scene;t->report->image_valid=false;
    for(unsigned plane=0;plane<2;plane++){
        t->report->plane=plane;
        int error=check(t);if(error)return error;
        uint32_t budget=remaining(t);if(!budget)return FPGA_LINK_TIMEOUT;
        error=fpga_wire_tile(t->wire,plane,t->tagged,t->work->body,budget);if(error)return error;
        for(unsigned i=0;i<1024;i++){
            t->report->pixel=i;
            error=check(t);if(error)return error;
            uint16_t received=(uint16_t)(t->work->body[8+2*i]|(uint16_t)t->work->body[9+2*i]<<8);
            uint16_t expected=batch?batch_expected(scene,plane,i%32,i/32,count):expected_pixel(scene,plane,i%32,i/32);
            if(received!=expected){t->report->expected=expected;t->report->received=received;return FPGA_LINK_DATA;}
            if(!plane)t->work->received_color[i]=received;
            t->report->pixels++;
        }
    }
    t->report->scenes++;t->report->image_valid=true;return 0;
}
static int fractal(test_t *t,unsigned index,bool measure)
{
    const fractal_config *f=&fractals[index];
    /* Avoid a full parameter array on stack. The seven fixed register writes
     * retain signed raw bits and the capability-supported legacy palette. */
    int error=rpc(t,FRACTAL_X0,(uint16_t)f->x0,NULL);
    if(!error)error=rpc(t,FRACTAL_Y0,(uint16_t)f->y0,NULL);
    if(!error)error=rpc(t,FRACTAL_DX,f->dx,NULL);
    if(!error)error=rpc(t,FRACTAL_DY,f->dy,NULL);
    if(!error)error=rpc(t,FRACTAL_CX,(uint16_t)f->cx,NULL);
    if(!error)error=rpc(t,FRACTAL_CY,(uint16_t)f->cy,NULL);
    if(!error)error=rpc(t,FRACTAL_CONFIG,(uint16_t)(f->limit|(uint16_t)f->julia<<8),NULL);
    if(!error)error=rpc(t,FRACTAL_DRAW,0,NULL);
    if(!error&&measure){
        uint16_t low=0,high=0;
        error=rpc(t,FRACTAL_CYCLES_LO,0,&low);
        if(!error)error=rpc(t,FRACTAL_CYCLES_HI,0,&high);
        if(!error){t->report->cycles[index]=(uint32_t)low|(uint32_t)high<<16;t->report->cycles_valid[index]=true;}
    }
    return error;
}
int fpga_tile_test_run(fpga_wire_t *wire,fpga_tile_test_workspace_t *work,
                       fpga_tile_test_report_t *report,bool tagged,bool include_fractals,uint32_t timeout)
{
    if(!report)return FPGA_LINK_INVALID;
    memset(report,0,sizeof *report);report->error=FPGA_LINK_INVALID;
    if(!wire||!wire->ops||!wire->ops->now_us||!work||!timeout||timeout>FPGA_TILE_TEST_TIMEOUT_MAX_US)return report->error;
    if(wire->busy){report->error=FPGA_LINK_BUSY;return report->error;}
    uint64_t start=wire->ops->now_us(wire->context);
    if(start>UINT64_MAX-timeout)return report->error;
    test_t t={wire,work,report,start+timeout,tagged};uint32_t base=wire->completed;
    int error=0;uint16_t word=0;
    if(tagged&&!wire->sync_capable){error=FPGA_LINK_UNSUPPORTED;goto done;}
    if(!wire->tile_capable){
        error=rpc(&t,TRI_TILE_CAPS,0,NULL);if(error)goto done;
        wire->tile_capable=true;
    }
    error=rpc(&t,TRI_INFO,0,NULL);if(error)goto done;
    report->scene=0;error=rpc(&t,TRI_CLEAR,0x0841,NULL);if(error)goto done;
    error=compare(&t,0,false,0);if(error)goto done;
    report->scene=1;report->image_valid=false;
    error=triangle(&t,first_triangle,0x1234,0xf800);if(error)goto done;
    error=compare(&t,1,false,0);if(error)goto done;
    report->scene=2;report->image_valid=false;
    error=rpc(&t,TRI_COLOR,0x07e0,NULL);if(error)goto done;
    error=rpc(&t,TRI_DRAW,0,&word);if(error)goto done;
    if(word){error=FPGA_LINK_DATA;goto done;}
    error=compare(&t,2,false,0);if(error)goto done;
    report->scene=3;report->image_valid=false;
    error=triangle(&t,near_triangle,0x2345,0x001f);if(error)goto done;
    error=compare(&t,3,false,0);if(error)goto done;
    if(include_fractals){
        error=rpc(&t,FRACTAL_INFO,0,&word);if(error)goto done;
        report->fractal_info=word;
        if((word&3)!=3){error=FPGA_LINK_UNSUPPORTED;goto done;}
        for(unsigned i=0;i<2;i++){
            report->scene=4+i;report->image_valid=false;
            error=fractal(&t,i,(word&4)!=0);if(error)goto done;
            error=compare(&t,4+i,false,0);if(error)goto done;
        }
    }
done:
    if(!error)error=check(&t);
    report->error=error;report->transactions=wire->completed-base;
    report->elapsed_us=wire->ops->now_us(wire->context)-start;
    if(error)report->image_valid=false;
    return error;
}

int fpga_tile_batch_test_run(fpga_wire_t *wire,fpga_tile_test_workspace_t *work,
                             fpga_tile_test_report_t *report,bool tagged,unsigned count,uint32_t timeout)
{
    if(!report)return FPGA_LINK_INVALID;
    memset(report,0,sizeof *report);report->error=FPGA_LINK_INVALID;
    if(!wire||!wire->ops||!wire->ops->now_us||!work||!timeout||timeout>FPGA_TILE_TEST_TIMEOUT_MAX_US)return report->error;
    if(count<1||count>64){report->error=FPGA_LINK_RANGE;return report->error;}
    if(wire->busy){report->error=FPGA_LINK_BUSY;return report->error;}
    uint64_t start=wire->ops->now_us(wire->context);
    if(start>UINT64_MAX-timeout)return report->error;
    test_t t={wire,work,report,start+timeout,tagged};uint32_t base=wire->completed;int error=0;
    if(tagged&&!wire->sync_capable){error=FPGA_LINK_UNSUPPORTED;goto done;}
    if(!wire->batch_capable){
        error=rpc(&t,TRI_BATCH_CAPS,0,NULL);if(error)goto done;
        wire->batch_capable=true;
    }
    if(!wire->tile_capable){
        error=rpc(&t,TRI_TILE_CAPS,0,NULL);if(error)goto done;
        wire->tile_capable=true;
    }
    error=rpc(&t,TRI_INFO,0,NULL);if(error)goto done;
    error=rpc(&t,TRI_CLEAR,0x0841,NULL);if(error)goto done;
    for(unsigned scene=0;scene<3;scene++){
        report->scene=scene;report->image_valid=false;
        for(unsigned i=0;i<count;i++){
            error=check(&t);if(error)goto done;
            uint16_t *r=work->records+10*i;
            unsigned x=(i%8)*64,y=(i/8)*64;
            uint16_t z=(uint16_t)(0x1000+i*257u),color=(uint16_t)((i*977u)^0x5a3cu);
            if(scene){color^=0xffff;if(scene==2)z=(uint16_t)(z+((i&1)?-1:1024));}
            r[0]=(uint16_t)x;r[1]=(uint16_t)y;r[2]=(uint16_t)(x+64);r[3]=(uint16_t)y;
            r[4]=(uint16_t)x;r[5]=(uint16_t)(y+64);r[6]=r[7]=r[8]=z;r[9]=color;
        }
        uint32_t budget=remaining(&t);if(!budget){error=FPGA_LINK_TIMEOUT;goto done;}
        error=fpga_wire_batch(wire,work->records,count,tagged,work->body,budget);if(error)goto done;
        error=compare(&t,scene,true,count);if(error)goto done;
    }
done:
    if(!error)error=check(&t);
    report->error=error;report->transactions=wire->completed-base;
    report->elapsed_us=wire->ops->now_us(wire->context)-start;
    if(error)report->image_valid=false;
    return error;
}
