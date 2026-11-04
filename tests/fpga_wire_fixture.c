#include "../drivers/fpga/fpga_wire.h"
#include "../third_party/carrier_link/link_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef NDEBUG
#error Regression assertions required
#endif
/* Independent bitwise CRC oracle, deliberately not carrier's table. */
static uint16_t crc_step(uint16_t c,const uint8_t *p,size_t n)
{
    while(n--){c^=(uint16_t)*p++<<8;for(unsigned i=0;i<8;i++)c=c&0x8000?(uint16_t)((c<<1)^0x1021):(uint16_t)(c<<1);}
    return c;
}
static void seal(uint8_t *p){uint16_t c=crc_step(0xffff,p,6);p[6]=(uint8_t)c;p[7]=(uint8_t)(c>>8);}
static void body_make(const uint8_t *q,uint8_t *r,size_t n)
{
    r[0]=0x5a;r[1]=0;r[2]=q[2];r[3]=2;
    switch(q[1]&127){
    case LINK_ID:r[4]=0x54;r[5]=0x50;break;
    case LINK_CAPS:r[4]=8;r[5]=1;break;
    case TRI_TILE_CAPS:r[4]=3;r[5]=1;break;
    case TRI_BATCH_CAPS:r[4]=64;r[5]=1;break;
    default:r[4]=q[4];r[5]=q[5];break;
    }
    seal(r);
    if(n>8){
        for(size_t i=8;i<n-2;i++)r[i]=(uint8_t)(i*37+q[4]*11+q[5]*19);
        uint16_t c=crc_step(crc_step(0xffff,r,6),r+8,n-10);r[n-2]=(uint8_t)c;r[n-1]=(uint8_t)(c>>8);
    }
}
typedef struct {
    uint64_t now;unsigned calls,offset;int fault;bool cancel,reenter;
    fpga_wire_t *wire;size_t last_tx,last_rx;
} fake_t;
static uint64_t now(void *p){return ((fake_t*)p)->now;}
static bool cancelled(void *p){return ((fake_t*)p)->cancel;}
static int exchange(void *p,const uint8_t *q,size_t tx,uint8_t *rx,size_t cap,size_t *received,uint64_t end)
{
    fake_t *f=p;f->calls++;f->last_tx=tx;f->last_rx=cap;
    uint16_t c=crc_step(0xffff,q,6);assert(q[6]==(uint8_t)c&&q[7]==(uint8_t)(c>>8));
    size_t n=(q[1]&127)==TRI_ROW?74:(q[1]&127)==TRI_TILE_READ?2058:8;
    if((q[1]&127)==TRI_BATCH){
        assert(tx==10u+q[4]*20u&&tx<=1290&&q[4]>=1&&q[4]<=64&&q[5]==1);
        c=crc_step(c,q+8,tx-10);assert(q[tx-2]==(uint8_t)c&&q[tx-1]==(uint8_t)(c>>8));
        for(unsigned i=0;i<q[4]*10u;i++){uint16_t word=(uint16_t)(i*997u+0x8000u);assert(q[8+i*2]==(uint8_t)word&&q[9+i*2]==(uint8_t)(word>>8));}
    }else assert(tx==8);
    assert(cap==n+((q[1]&128)?f->wire->capture_margin:0));
    if(f->reenter){uint8_t id[2]={1,2};assert(fpga_wire_id(f->wire,id,1000)==FPGA_LINK_BUSY);assert(id[0]==1&&id[1]==2);assert(fpga_wire_caps(f->wire,1000)==FPGA_LINK_BUSY);assert(fpga_wire_transport(f->wire,f->wire->ops,p)==FPGA_LINK_BUSY);}
    memset(rx,0xc3,cap);unsigned at=q[1]&128?f->offset:0;assert(at+n<=cap);body_make(q,rx+at,n);*received=cap;
    f->now+=17;
    switch(f->fault){
    case 1:rx[at+n-1]^=1;break;
    case 2:*received=cap+1;break;
    case 3:*received=cap-1;break;
    case 4:f->now=end;break;
    case 5:f->cancel=true;break;
    case 6:return FPGA_LINK_IO;
    case 7:rx[at+2]++;seal(rx+at);break;
    case 8:rx[at+1]=2;seal(rx+at);*received=8;break;
    case 9:if(n==8&&cap>=16)body_make(q,rx+at+8,n);break;
    case 10:return 1;
    case 11:rx[at+4]^=1;seal(rx+at);break;
    default:break;
    }
    return 0;
}
static const fpga_wire_ops_t ops={exchange,now,cancelled};
static void engine(void)
{
    fpga_wire_workspace_t space;fpga_wire_t w;fake_t f={.now=100,.wire=&w};uint8_t out[2058],id[2]={0};
    assert(!fpga_wire_init(&w,&ops,&f,&space));
    assert(fpga_wire_command(&w,LINK_ID,0,0,true,out,8,1000)==FPGA_LINK_UNSUPPORTED&&!f.calls);
    assert(!fpga_wire_id(&w,id,1000)&&id[0]==0x54&&id[1]==0x50&&f.calls==1);
    assert(!fpga_wire_caps(&w,1000)&&w.sync_capable);
    assert(!fpga_wire_tile_caps(&w,1000)&&w.tile_capable);
    assert(!fpga_wire_batch_caps(&w,1000)&&w.batch_capable);
    uint8_t seq=w.sequence;assert(!fpga_wire_transport(&w,&ops,&f));assert(w.sequence==seq&&w.sync_capable&&w.tile_capable&&w.batch_capable);
    f.reenter=true;
    for(unsigned margin=8;margin<=16;margin+=8){
        assert(!fpga_wire_margin(&w,margin));
        for(f.offset=0;f.offset<=margin;f.offset++)for(unsigned plane=0;plane<2;plane++){
            assert(!fpga_wire_command(&w,LINK_ECHO,0x55,0xaa,true,out,8,1000)&&out[4]==0x55&&out[5]==0xaa);
            assert(!fpga_wire_row(&w,31,plane,true,out,1000)&&out[4]==31&&out[5]==plane);
            assert(!fpga_wire_tile(&w,plane,true,out,1000)&&out[4]==0&&out[5]==plane);
        }
    }
    f.offset=0;
    uint16_t records[640];for(unsigned i=0;i<640;i++)records[i]=(uint16_t)(i*997u+0x8000u);
    for(unsigned n=1;n<=64;n++)for(unsigned tag=0;tag<2;tag++)assert(!fpga_wire_batch(&w,records,n,tag!=0,out,1000)&&out[4]==n&&out[5]==1);
    assert(f.last_tx==1290);
    unsigned calls=f.calls;uint32_t attempted=w.attempted;
    assert(fpga_wire_batch(&w,records,0,false,out,1000)==FPGA_LINK_INVALID);
    assert(fpga_wire_batch(&w,records,65,false,out,1000)==FPGA_LINK_INVALID);
    assert(fpga_wire_batch(&w,NULL,1,false,out,1000)==FPGA_LINK_INVALID);
    assert(fpga_wire_command(&w,TRI_BATCH,1,1,false,out,8,1000)==FPGA_LINK_INVALID);
    assert(fpga_wire_row(&w,32,0,false,out,1000)==FPGA_LINK_RANGE);
    assert(fpga_wire_tile(&w,2,false,out,1000)==FPGA_LINK_RANGE);
    assert(fpga_wire_margin(&w,9)==FPGA_LINK_INVALID);
    assert(fpga_wire_id(&w,space.capture,1000)==FPGA_LINK_INVALID);
    assert(fpga_wire_command(&w,LINK_ID,0,0,false,space.request,8,1000)==FPGA_LINK_INVALID);
    assert(fpga_wire_command(&w,LINK_ID,0,0,false,(uint8_t*)&w,8,1000)==FPGA_LINK_INVALID);
    assert(f.calls==calls&&w.attempted==attempted);
    assert(!fpga_wire_first_failure(&w));
    /* Original24 bytes, not canonical8 or reconstructed response. */
    f.fault=1;f.offset=8;memset(out,0xa6,sizeof out);
    assert(fpga_wire_command(&w,LINK_ECHO,0,255,true,out,8,1000)==FPGA_LINK_DATA);
    const fpga_wire_failure_t *failure=fpga_wire_first_failure(&w);
    assert(failure&&failure->valid&&!failure->truncated&&failure->saved_length==24);
    assert(failure->request_length==8&&failure->received_length==24&&failure->error==FPGA_LINK_DATA);
    uint8_t expected[24];memset(expected,0xc3,sizeof expected);
    body_make(failure->request_header,expected+8,8);expected[15]^=1;
    assert(!memcmp(expected,failure->raw,24));
    for(unsigned i=0;i<sizeof out;i++)assert(out[i]==0xa6);
    fpga_wire_failure_t first=*failure;
    f.fault=0;assert(!fpga_wire_command(&w,LINK_ECHO,1,254,true,out,8,1000));
    assert(!memcmp(&first,fpga_wire_first_failure(&w),sizeof first));
    f.offset=0;
    for(unsigned tag=0;tag<2;tag++)for(f.fault=1;f.fault<=11;f.fault++){
        if(f.fault==8&&tag)continue;
        if(f.fault==9&&!tag)continue;
        memset(out,0xa6,sizeof out);f.cancel=false;
        assert(fpga_wire_command(&w,LINK_ECHO,7,19,tag!=0,out,8,1000)<0);
        for(unsigned i=0;i<sizeof out;i++)assert(out[i]==0xa6);
        assert(!w.busy);
    }
    f.fault=0;f.cancel=false;w.sequence=255;
    assert(!fpga_wire_id(&w,id,1000)&&w.sequence==0);assert(!fpga_wire_id(&w,id,1000)&&w.sequence==1);
    f.fault=8;assert(fpga_wire_caps(&w,1000)==FPGA_LINK_UNSUPPORTED&&!w.sync_capable);f.fault=0;
    calls=f.calls;f.cancel=true;memset(out,0xa6,sizeof out);
    assert(fpga_wire_command(&w,LINK_ID,0,0,false,out,8,1000)==FPGA_LINK_CANCELLED&&f.calls==calls&&out[0]==0xa6);
    f.cancel=false;f.now=UINT64_MAX-2;
    assert(fpga_wire_id(&w,id,4)==FPGA_LINK_INVALID&&f.calls==calls);
    assert(w.attempted==w.completed+w.failed);
    assert(!memcmp(&first,fpga_wire_first_failure(&w),sizeof first));
    f.now=100;f.fault=1;f.cancel=false;
    assert(!fpga_wire_init(&w,&ops,&f,&space)&&!fpga_wire_first_failure(&w));
    w.sync_capable=true;w.tile_capable=true;
    assert(fpga_wire_tile(&w,0,true,out,1000)==FPGA_LINK_DATA);
    failure=fpga_wire_first_failure(&w);
    assert(failure&&failure->truncated&&failure->received_length==2074&&failure->saved_length==96);
    assert(!memcmp(failure->raw,space.capture,96));
    puts("PASS first-failure diagnostics: original24 bytes, unchanged caller, immutable firstfailure, reset oninit, bounded2074-byte truncation");
    puts("PASS wire engine: exact finite lengths, CAPS, transport switch, all prefix offsets, ROW/TILE planes, batch1..64, limits, failure atomicity, cancellation/deadline/reentry/sequence");
}
static unsigned decode(const char *s,uint8_t *out,size_t cap)
{
    size_t n=strlen(s);assert(!(n&1)&&n/2<=cap);for(size_t i=0;i<n/2;i++){unsigned b;assert(sscanf(s+2*i,"%2x",&b)==1);out[i]=(uint8_t)b;}return (unsigned)(n/2);
}
int main(int argc,char **argv)
{
    if(argc==2&&!strcmp(argv[1],"--validate")){
        char qhex[17],rawhex[4149];unsigned n;
        while(scanf("%16s %4148s %u",qhex,rawhex,&n)==3){
            uint8_t q[8],raw[2074],out[2060];assert(decode(qhex,q,8)==8);unsigned len=decode(rawhex,raw,2074);assert(n<=2058);
            memset(out,0xa6,sizeof out);unsigned offset=9999;
            int rc=fpga_wire_validate(q,raw,len,out+1,n,&offset);
            assert(out[0]==0xa6&&out[n+1]==0xa6);
            uint32_t hash=2166136261u;
            if(rc){assert(offset==9999);for(unsigned i=0;i<sizeof out;i++)assert(out[i]==0xa6);}else for(unsigned i=0;i<n;i++)hash=(hash^out[1+i])*16777619u;
            printf("%d %u %u\n",rc,offset,hash);
        }
        return 0;
    }
    engine();return 0;
}
