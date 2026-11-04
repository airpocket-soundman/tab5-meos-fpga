#include "../drivers/fpga/fpga_tile_test.h"
#include "../third_party/carrier_link/link_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error Assertions required
#endif
static uint16_t oracle[64][3][2][1024];
typedef struct {
    uint64_t now;unsigned count,batches,scene,plane,calls,max_tx,cancel_checks;
    int fault;bool tagged,cancel_pending;
} model_t;
static uint64_t now(void *p){return ++((model_t*)p)->now;}
static bool cancelled(void *p){model_t *m=p;return m->cancel_pending&&++m->cancel_checks>=4;}
static uint16_t crc(uint16_t c,const uint8_t *p,size_t n)
{while(n--){c^=(uint16_t)*p++<<8;for(unsigned i=0;i<8;i++)c=c&0x8000?(uint16_t)((c<<1)^0x1021):(uint16_t)(c<<1);}return c;}
static int exchange(void *p,const uint8_t *q,size_t tx,uint8_t *raw,size_t cap,size_t *received,uint64_t deadline)
{
    model_t *m=p;m->calls++;if(tx>m->max_tx)m->max_tx=(unsigned)tx;
    assert(!!(q[1]&128)==m->tagged);unsigned op=q[1]&127;uint16_t result=0;uint8_t status=0;
    uint16_t c=crc(0xffff,q,6);assert(q[6]==(uint8_t)c&&q[7]==(uint8_t)(c>>8));
    if(op==TRI_BATCH_CAPS){result=0x0140;if(m->fault==1)status=2;}
    else if(op==TRI_TILE_CAPS)result=0x0103;
    else if(op==TRI_INFO)result=0x2020;
    else if(op==TRI_CLEAR){assert(q[4]==0x41&&q[5]==8);m->batches=0;result=0;}
    else if(op==TRI_BATCH){
        assert(q[4]==m->count&&q[5]==1&&tx==10+m->count*20&&tx<=1290&&m->batches<3);
        c=crc(c,q+8,tx-10);assert(q[tx-2]==(uint8_t)c&&q[tx-1]==(uint8_t)(c>>8));
        m->scene=m->batches++;
        for(unsigned i=0;i<m->count;i++){
            unsigned x=(i%8)*64,y=(i/8)*64;uint16_t z=(uint16_t)(0x1000+i*257),color=(uint16_t)((i*977)^0x5a3c);
            if(m->scene){color^=0xffff;if(m->scene==2)z=(uint16_t)(z+((i&1)?-1:1024));}
            const uint16_t expected[10]={(uint16_t)x,(uint16_t)y,(uint16_t)(x+64),(uint16_t)y,(uint16_t)x,(uint16_t)(y+64),z,z,z,color};
            for(unsigned j=0;j<10;j++){unsigned at=8+20*i+2*j;assert(q[at]==(uint8_t)expected[j]&&q[at+1]==(uint8_t)(expected[j]>>8));}
        }
        result=(uint16_t)(m->count|0x100);
        if(m->fault==2&&m->scene==1)result^=1;
    }else {assert(op==TRI_TILE_READ&&q[4]==0&&q[5]<2);m->plane=q[5];result=(uint16_t)(m->plane<<8);}
    if(op!=TRI_BATCH)assert(tx==8);
    size_t n=op==TRI_TILE_READ?2058:8;assert(cap==n+(m->tagged?16:0));
    size_t at=m->tagged?11:0;memset(raw,0xc3,cap);uint8_t *r=raw+at;
    r[0]=0x5a;r[1]=status;r[2]=q[2];r[3]=2;r[4]=(uint8_t)result;r[5]=(uint8_t)(result>>8);
    c=crc(0xffff,r,6);r[6]=(uint8_t)c;r[7]=(uint8_t)(c>>8);
    if(n>8){
        for(unsigned i=0;i<1024;i++){
            uint16_t v=oracle[m->count-1][m->scene][m->plane][i];
            if(m->fault==3&&m->scene==1&&!m->plane&&i==701)v^=1;
            r[8+2*i]=(uint8_t)v;r[9+2*i]=(uint8_t)(v>>8);
        }
        c=crc(c,r+8,2048);r[2056]=(uint8_t)c;r[2057]=(uint8_t)(c>>8);
        if(m->fault==4&&m->scene==1)r[2057]^=1;
        if(m->fault==5&&m->scene==1&&!m->plane){m->cancel_pending=true;m->cancel_checks=0;}
        if(m->fault==6&&m->scene==1)m->now=deadline;
    }
    m->now+=20;*received=cap;return 0;
}
static const fpga_wire_ops_t ops={exchange,now,cancelled};
static fpga_wire_workspace_t ws;
static fpga_tile_test_workspace_t gpu;
int main(int argc,char **argv)
{
    assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);
    for(unsigned n=0;n<64;n++)for(unsigned s=0;s<3;s++)for(unsigned p=0;p<2;p++)for(unsigned i=0;i<1024;i++){
        int lo=fgetc(file),hi=fgetc(file);assert(lo>=0&&hi>=0);oracle[n][s][p][i]=(uint16_t)(lo|(unsigned)hi<<8);
    }
    assert(fgetc(file)==EOF);fclose(file);
    for(unsigned tag=0;tag<2;tag++)for(unsigned n=1;n<=64;n++){
        model_t m={.count=n,.tagged=tag!=0};fpga_wire_t wire;fpga_tile_test_report_t report;
        assert(!fpga_wire_init(&wire,&ops,&m,&ws));wire.sync_capable=tag!=0;
        assert(!fpga_tile_batch_test_run(&wire,&gpu,&report,tag!=0,n,4000000));
        assert(report.scenes==3&&report.pixels==6144&&report.image_valid&&report.transactions==13&&m.batches==3&&m.max_tx==10+20*n);
        for(unsigned i=0;i<1024;i++)assert(gpu.received_color[i]==oracle[n-1][2][0][i]);
    }
    for(unsigned tag=0;tag<2;tag++)for(int fault=1;fault<=6;fault++){
        model_t m={.count=64,.tagged=tag!=0,.fault=fault};fpga_wire_t wire;fpga_tile_test_report_t report;
        assert(!fpga_wire_init(&wire,&ops,&m,&ws));wire.sync_capable=tag!=0;
        int rc=fpga_tile_batch_test_run(&wire,&gpu,&report,tag!=0,64,4000000);
        assert(rc<0&&report.error==rc&&!report.image_valid);
        if(fault==1)assert(!report.scenes&&!report.pixels&&!m.batches);
        if(fault==2)assert(report.scenes==1&&m.batches==2);
        if(fault==3)assert(rc==FPGA_LINK_DATA&&report.scene==1&&report.pixel==701&&report.expected!=report.received);
        if(fault==4)assert(report.scenes==1&&report.pixels==2048);
        if(fault==5)assert(rc==FPGA_LINK_CANCELLED&&report.scenes==1&&report.pixels==2049);
        if(fault==6)assert(rc==FPGA_LINK_TIMEOUT&&report.scenes==1);
    }
    model_t m={.count=64};fpga_wire_t wire;fpga_tile_test_report_t report;assert(!fpga_wire_init(&wire,&ops,&m,&ws));
    assert(fpga_tile_batch_test_run(&wire,&gpu,&report,false,0,100000)==FPGA_LINK_RANGE&&!m.calls);
    assert(fpga_tile_batch_test_run(&wire,&gpu,&report,false,65,100000)==FPGA_LINK_RANGE&&!m.calls);
    assert(fpga_tile_batch_test_run(&wire,&gpu,&report,true,64,100000)==FPGA_LINK_UNSUPPORTED&&!m.calls);
    puts("PASS bulk batch diagnostic: allcounts1..64 tagged+untagged, max1290-byte TX, 3scenes/6144words each, tie/near/far correctness, strict CRC/pixel/ack/cancel/deadline failure and actual-image retention");
    return 0;
}
