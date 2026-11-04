#include "../drivers/fpga/fpga_surface.h"
#include "../third_party/carrier_link/link_protocol.h"
#include <m5os/display.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error Assertions required
#endif
static uint16_t golden[1024];
static fpga_wire_workspace_t wire_workspace;
static fpga_surface_workspace_t surface_workspace;
static bool display_locked,fail_begin,fail_end;
static unsigned starts,draws,ends;
static fpga_surface_t *presenting;
bool display_begin_frame(void){starts++;if(fail_begin)return false;assert(!display_locked);display_locked=true;return true;}
void display_draw_bitmap_scaled(int x,int y,int w,int h,const uint16_t *pixels,int scale)
{
    assert(display_locked&&w==32&&h==32&&x>=0&&y>=0&&scale>=1);draws++;
    assert(pixels==surface_workspace.color);
    assert(fpga_surface_present(presenting,0,0,1)==FPGA_LINK_BUSY);
    for(unsigned i=0;i<1024;i++)assert(pixels[i]==golden[i]);
}
bool display_end_frame(void){assert(display_locked);display_locked=false;ends++;return !fail_end;}
typedef struct {
    uint64_t now;uint16_t background;
    unsigned calls,batch_calls,max_tx,cancel_checks;
    int fault;bool tagged,batched,cancel_pending;
    fpga_surface_t *surface;
} device_t;
static uint64_t now(void *p){return ++((device_t*)p)->now;}
static bool cancelled(void *p){device_t *d=p;return d->cancel_pending&&++d->cancel_checks>=4;}
static uint16_t crc(const uint8_t *p,size_t n,uint16_t c)
{while(n--){c^=(uint16_t)*p++<<8;for(unsigned j=0;j<8;j++)c=c&0x8000?(uint16_t)((c<<1)^0x1021):(uint16_t)(c<<1);}return c;}
static int exchange(void *p,const uint8_t *q,size_t tn,uint8_t *raw,size_t cap,size_t *received,uint64_t deadline)
{
    device_t *d=p;assert(!display_locked);assert(!!(q[1]&128)==d->tagged);d->calls++;
    if(tn>d->max_tx)d->max_tx=(unsigned)tn;
    assert(fpga_surface_begin(d->surface,0,1000)==FPGA_LINK_BUSY);
    assert(fpga_surface_finish(d->surface,1000)==FPGA_LINK_BUSY);
    uint64_t generation=777;assert(!fpga_surface_image(d->surface,&generation)&&generation==777);
    unsigned op=q[1]&127;uint16_t value=0;uint8_t status=0;
    if(op==TRI_TILE_CAPS){assert(tn==8);value=d->fault==6?0x0203:0x0103;if(d->fault==1)status=2;}
    else if(op==TRI_BATCH_CAPS){assert(tn==8);value=d->fault==7?0x013f:0x0140;}
    else if(op==TRI_INFO){assert(tn==8);value=d->fault==8?0x2021:0x2020;}
    else if(op==TRI_CLEAR){assert(tn==8);d->background=(uint16_t)(q[4]|(uint16_t)q[5]<<8);d->batched=false;value=0;}
    else if(op==TRI_BATCH){
        assert(q[4]==64&&q[5]==1&&tn==1290);d->batched=true;d->batch_calls++;value=0x0140;
        for(unsigned i=0;i<64;i++){
            unsigned x=i%8*64,y=i/8*64;uint16_t z=(uint16_t)(0x1000+i*257),color=(uint16_t)((i*977)^0x5a3c);
            const uint16_t words[10]={(uint16_t)x,(uint16_t)y,(uint16_t)(x+64),(uint16_t)y,(uint16_t)x,(uint16_t)(y+64),z,z,z,color};
            for(unsigned k=0;k<10;k++){size_t at=8+20*i+2*k;assert(q[at]==(uint8_t)words[k]&&q[at+1]==(uint8_t)(words[k]>>8));}
        }
        uint16_t c=crc(q+8,1280,crc(q,6,0xffff));assert(q[1288]==(uint8_t)c&&q[1289]==(uint8_t)(c>>8));
    }else {assert(op==TRI_TILE_READ&&tn==8&&q[4]==0&&q[5]==0);}
    size_t n=op==TRI_TILE_READ?2058:8;assert(cap==n+(d->tagged?16:0));
    size_t at=d->tagged?5:0;memset(raw,0xc3,cap);uint8_t *r=raw+at;
    r[0]=0x5a;r[1]=status;r[2]=q[2];r[3]=2;r[4]=(uint8_t)value;r[5]=(uint8_t)(value>>8);
    uint16_t c=crc(r,6,0xffff);r[6]=(uint8_t)c;r[7]=(uint8_t)(c>>8);
    if(n>8){
        for(unsigned i=0;i<1024;i++){uint16_t pixel=d->batched?golden[i]:d->background;r[8+2*i]=(uint8_t)pixel;r[9+2*i]=(uint8_t)(pixel>>8);}
        c=crc(r+8,2048,c);r[2056]=(uint8_t)c;r[2057]=(uint8_t)(c>>8);
        if(d->fault==2)r[2057]^=1;
        if(d->fault==3)return FPGA_LINK_IO;
        if(d->fault==4)d->now=deadline;
        if(d->fault==5){d->cancel_pending=true;d->cancel_checks=0;}
    }
    *received=cap;d->now+=20;return 0;
}
static const fpga_wire_ops_t ops={exchange,now,cancelled,false};
static void geometry(fpga_surface_t *s)
{
    for(unsigned i=0;i<64;i++){
        int16_t x=(int16_t)(i%8*64),y=(int16_t)(i/8*64);uint16_t z=(uint16_t)(0x1000+i*257);
        fpga_surface_triangle_t triangle={x,y,(int16_t)(x+64),y,x,(int16_t)(y+64),z,z,z,(uint16_t)((i*977)^0x5a3c)};
        assert(!fpga_surface_queue(s,&triangle));
    }
}
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    for(unsigned i=0;i<1024;i++){int low=fgetc(f),high=fgetc(f);assert(low>=0&&high>=0);golden[i]=(uint16_t)(low|(unsigned)high<<8);}assert(fgetc(f)==EOF);fclose(f);
    for(unsigned tag=0;tag<2;tag++){
        device_t d={.tagged=tag!=0};fpga_wire_t wire;fpga_surface_t s;
        assert(!fpga_wire_init(&wire,&ops,&d,&wire_workspace));wire.sync_capable=tag!=0;
        assert(!fpga_surface_init(&s,&wire,&surface_workspace,tag!=0));d.surface=&s;presenting=&s;
        assert(fpga_surface_finish(&s,1000)==FPGA_LINK_UNINITIALIZED);
        uint64_t generation=99;assert(!fpga_surface_image(&s,&generation)&&generation==99);
        assert(!fpga_surface_begin(&s,0x0841,1000000));assert(s.active&&!s.image_valid&&s.generation==1);
        unsigned before=d.calls;geometry(&s);assert(s.queued==64&&d.calls==before);
        fpga_surface_triangle_t extra={0};assert(fpga_surface_queue(&s,&extra)==FPGA_LINK_BUSY&&s.queued==64&&d.calls==before);
        assert(fpga_surface_begin(&s,0,1000)==FPGA_LINK_BUSY&&s.queued==64);
        assert(!fpga_surface_flush(&s,1000000));assert(s.batches==1&&s.triangles==64&&!s.queued&&d.max_tx==1290);
        before=d.calls;assert(!fpga_surface_flush(&s,1000)&&d.calls==before);
        assert(!fpga_surface_finish(&s,1000000)&&s.image_valid&&!s.active&&s.readbacks==1);
        const uint16_t *image=fpga_surface_image(&s,&generation);assert(image==surface_workspace.color&&generation==1);
        for(unsigned i=0;i<1024;i++)assert(image[i]==golden[i]);
        before=d.calls;
        assert(fpga_surface_present(&s,-1,0,1)==FPGA_LINK_RANGE);
        assert(fpga_surface_present(&s,0,0,0xffffffffu)==FPGA_LINK_RANGE);
        fail_begin=true;unsigned old_ends=ends;assert(fpga_surface_present(&s,0,0,1)==FPGA_LINK_BUSY&&!s.presented&&s.image_valid&&ends==old_ends);fail_begin=false;
        fail_end=true;assert(fpga_surface_present(&s,0,0,1)==FPGA_LINK_IO&&!s.presented&&s.image_valid);fail_end=false;
        assert(!fpga_surface_present(&s,3,4,2)&&s.presented&&s.presentations==1&&d.calls==before);
        assert(fpga_surface_begin(&s,0,0)==FPGA_LINK_INVALID&&s.image_valid&&s.generation==1);
        s.generation=UINT64_MAX;assert(fpga_surface_begin(&s,0,1000)==FPGA_LINK_RANGE&&s.image_valid);s.generation=1;
        assert(!fpga_surface_begin(&s,0xffff,1000000)&&!s.image_valid&&s.generation==2);
        assert(!fpga_surface_finish(&s,1000000));image=fpga_surface_image(&s,&generation);assert(generation==2);
        for(unsigned i=0;i<1024;i++)assert(image[i]==0xffff);
    }
    for(unsigned tag=0;tag<2;tag++)for(int fault=1;fault<=8;fault++){
        device_t d={.tagged=tag!=0,.fault=fault};fpga_wire_t wire;fpga_surface_t s;
        assert(!fpga_wire_init(&wire,&ops,&d,&wire_workspace));wire.sync_capable=tag!=0;
        assert(!fpga_surface_init(&s,&wire,&surface_workspace,tag!=0));d.surface=&s;
        int error=fpga_surface_begin(&s,0x0841,1000000);
        if(!error){geometry(&s);error=fpga_surface_finish(&s,1000000);}
        assert(error<0&&s.poisoned&&!s.image_valid&&!s.busy&&!s.active&&!fpga_surface_image(&s,NULL));
        unsigned before=d.calls;assert(fpga_surface_begin(&s,0,1000)==FPGA_LINK_IO&&d.calls==before);
        assert(fpga_surface_present(&s,0,0,1)==FPGA_LINK_UNINITIALIZED);
    }
    assert(starts&&draws&&ends&&!display_locked);
    puts("PASS surface lifecycle: explicit64-record batch, independent actual readback, generations, bounds, no display-lock during wire, displayfailures, poison/no replay, cancellation/deadline/reentry and no fallback");
    return 0;
}
