#include "../drivers/fpga/fpga_tile_test.h"
#include "../third_party/carrier_link/link_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error Assertions required
#endif
static uint16_t oracle[6][2][1024];
typedef struct {
    uint64_t now;unsigned scene,plane,calls,cap_calls,cancel_checks;
    uint16_t registers[10],fractal[7];
    int fault;bool tagged,cancel_pending,no_cycles;
} device_t;
static uint64_t now(void *p){device_t *d=p;return ++d->now;}
static bool cancelled(void *p)
{
    device_t *d=p;return d->cancel_pending&&++d->cancel_checks>=4;
}
static uint16_t crc(const uint8_t *p,size_t n,uint16_t c)
{while(n--){c^=(uint16_t)*p++<<8;for(unsigned i=0;i<8;i++)c=c&0x8000?(uint16_t)((c<<1)^0x1021):(uint16_t)(c<<1);}return c;}
static unsigned coverage(unsigned scene,uint16_t depth)
{unsigned n=0;for(unsigned i=0;i<1024;i++)if(oracle[scene][1][i]==depth)n++;return n;}
static int exchange(void *p,const uint8_t *q,size_t tn,uint8_t *raw,size_t cap,size_t *received,uint64_t deadline)
{
    device_t *d=p;assert(tn==8);unsigned op=q[1]&127;uint16_t v=(uint16_t)(q[4]|(uint16_t)q[5]<<8),result=v;uint8_t status=0;
    assert(!!(q[1]&128)==d->tagged);d->calls++;
    if(op==TRI_TILE_CAPS){d->cap_calls++;result=0x0103;if(d->fault==1)status=2;}
    else if(op==TRI_INFO)result=0x2020;
    else if(op==TRI_CLEAR){assert(v==0x0841);d->scene=0;result=0;}
    else if(op>=TRI_AX&&op<=TRI_COLOR)d->registers[op-TRI_AX]=v;
    else if(op==TRI_DRAW){
        assert(!v);
        if(d->registers[9]==0xf800){d->scene=1;result=(uint16_t)coverage(1,0x1234);}
        else if(d->registers[9]==0x07e0){d->scene=2;result=0;}
        else {assert(d->registers[9]==0x001f);d->scene=3;result=(uint16_t)coverage(3,0x2345);}
        if(d->fault==5&&d->scene==1)result++;
    }else if(op==FRACTAL_INFO){result=d->no_cycles?0x0103:0x021f;if(d->fault==6)status=2;}
    else if(op>=FRACTAL_X0&&op<=FRACTAL_CONFIG)d->fractal[op-FRACTAL_X0]=v;
    else if(op==FRACTAL_DRAW){
        assert(!v&&d->fractal[2]>0&&d->fractal[3]>0&&d->fractal[6]%256==64);
        d->scene=4+((d->fractal[6]>>8)&1);result=1024;
    }else if(op==FRACTAL_CYCLES_LO){assert(!d->no_cycles);result=(uint16_t)(0x5678+d->scene-4);}
    else if(op==FRACTAL_CYCLES_HI){assert(!d->no_cycles);result=0x1234;}
    else {assert(op==TRI_TILE_READ&&q[4]==0&&q[5]<2);d->plane=q[5];}
    size_t body=op==TRI_TILE_READ&&!status?2058:8;
    assert(cap==(op==TRI_TILE_READ?2058:8)+(d->tagged?16:0));
    size_t at=d->tagged?7:0;memset(raw,0xc3,cap);uint8_t *r=raw+at;
    r[0]=0x5a;r[1]=status;r[2]=q[2];r[3]=2;r[4]=(uint8_t)result;r[5]=(uint8_t)(result>>8);
    uint16_t c=crc(r,6,0xffff);r[6]=(uint8_t)c;r[7]=(uint8_t)(c>>8);
    if(body>8){
        for(unsigned i=0;i<1024;i++){
            uint16_t word=oracle[d->scene][d->plane][i];
            if(d->fault==2&&d->scene==1&&d->plane==0&&i==701)word^=1; /*validCRC, wrong pixel*/
            r[8+2*i]=(uint8_t)word;r[9+2*i]=(uint8_t)(word>>8);
        }
        c=crc(r+8,2048,c);r[2056]=(uint8_t)c;r[2057]=(uint8_t)(c>>8);
        if(d->fault==3&&d->scene==1)r[2057]^=1;
        if(d->fault==4&&d->scene==1&&d->plane==0){d->cancel_pending=true;d->cancel_checks=0;}
        if(d->fault==7&&d->scene==1)d->now=deadline;
    }
    *received=cap;d->now+=20;return 0;
}
static const fpga_wire_ops_t ops={exchange,now,cancelled,false};
static fpga_wire_workspace_t wire_space;
static fpga_tile_test_workspace_t tile_space;
int main(int argc,char **argv)
{
    assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);
    for(unsigned s=0;s<6;s++)for(unsigned p=0;p<2;p++)for(unsigned i=0;i<1024;i++){
        int lo=fgetc(file),hi=fgetc(file);assert(lo>=0&&hi>=0);oracle[s][p][i]=(uint16_t)(lo|(unsigned)hi<<8);
    }
    assert(fgetc(file)==EOF);fclose(file);
    for(unsigned tag=0;tag<2;tag++)for(int fault=0;fault<=7;fault++){
        fpga_wire_t wire;device_t d={.tagged=tag!=0,.fault=fault};fpga_tile_test_report_t report;
        assert(!fpga_wire_init(&wire,&ops,&d,&wire_space));wire.sync_capable=tag!=0;
        memset(&tile_space,0xa5,sizeof tile_space);
        int rc=fpga_tile_test_run(&wire,&tile_space,&report,tag!=0,true,4000000);
        assert(d.cap_calls==1);
        if(!fault){
            assert(!rc&&report.scenes==6&&report.pixels==12288&&report.image_valid&&report.transactions==60);
            assert(report.cycles_valid[0]&&report.cycles_valid[1]&&report.cycles[0]==0x12345678&&report.cycles[1]==0x12345679);
            for(unsigned i=0;i<1024;i++)assert(tile_space.received_color[i]==oracle[5][0][i]);
            d.no_cycles=true;wire.tile_capable=false;
            assert(!fpga_tile_test_run(&wire,&tile_space,&report,tag!=0,true,4000000));
            assert(report.scenes==6&&!report.cycles_valid[0]&&!report.cycles_valid[1]&&report.transactions==56);
            assert(!fpga_tile_test_run(&wire,&tile_space,&report,tag!=0,false,4000000));
            assert(report.scenes==4&&report.pixels==8192&&report.image_valid&&report.transactions==34);
        }else {
            assert(rc<0&&report.error==rc&&!report.image_valid);
            if(fault==1)assert(rc==(tag?FPGA_LINK_DATA:FPGA_LINK_UNSUPPORTED)&&report.scenes==0&&report.pixels==0);
            if(fault==2)assert(rc==FPGA_LINK_DATA&&report.scene==1&&report.plane==0&&report.pixel==701&&report.expected!=report.received);
            if(fault==3)assert(report.scenes==1&&report.pixels==2048);
            if(fault==4)assert(rc==FPGA_LINK_CANCELLED&&report.scenes==1&&report.pixels==2049);
            if(fault==5)assert(rc==FPGA_LINK_DATA&&report.scenes==1);
            if(fault==6)assert(rc==(tag?FPGA_LINK_DATA:FPGA_LINK_UNSUPPORTED)&&report.scenes==4);
            if(fault==7)assert(rc==FPGA_LINK_TIMEOUT&&report.scenes==1);
        }
    }
    fpga_wire_t wire;device_t d={0};fpga_tile_test_report_t report;
    assert(!fpga_wire_init(&wire,&ops,&d,&wire_space));
    assert(fpga_tile_test_run(&wire,&tile_space,&report,true,true,1000)==FPGA_LINK_UNSUPPORTED&&!d.calls);
    assert(fpga_tile_test_run(&wire,&tile_space,&report,false,true,0)==FPGA_LINK_INVALID);
    assert(fpga_tile_test_run(&wire,&tile_space,&report,false,true,30000001)==FPGA_LINK_INVALID);
    assert(fpga_tile_test_run(&wire,&tile_space,&report,false,true,10)==FPGA_LINK_TIMEOUT&&!report.image_valid);
    printf("PASS bulk GPU diagnostics: 6scenes/12288pixels tagged+untagged; independent golden color+depth, CRC-valid pixel corruption, CRC, cancellation, deadline, unsupported CAPS/fractal, draw-count, optionalcycles and actual readback retention\n");
    return 0;
}
