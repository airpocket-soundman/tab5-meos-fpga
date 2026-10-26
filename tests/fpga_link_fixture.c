#include <m5os/fpga_link.h>
#include "../drivers/fpga/fpga_mock.h"
#include "../drivers/fpga/fpga_selftest.h"
#include "../user/fpga/fpga_app.h"
#ifdef NDEBUG
#error Regression assertions must be enabled
#endif
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
void kprintf(const char *format,...){va_list args;va_start(args,format);vprintf(format,args);va_end(args);}
static int bad_read(void*p,uint8_t*d,size_t n,uint64_t t){(void)p;(void)d;(void)t;return (int)n+1;}
static int late_read(void*p,uint8_t*d,size_t n,uint64_t t){int count=fpga_mock_ops.read(p,d,n,t);((fpga_mock_t*)p)->now=t;return count;}
static int cancel_turn(void*p,uint64_t t){int result=fpga_mock_ops.turnaround(p,t);((fpga_mock_t*)p)->cancel=true;return result;}
static fpga_link_t *recursive;
static int reenter(void*p,uint64_t t){uint8_t out[2];assert(fpga_link_exchange(recursive,1,0,0,out,100000)==FPGA_LINK_BUSY);return fpga_mock_ops.begin(p,t);}
static void seal(uint8_t f[8]){uint16_t c=fpga_link_crc16(f,6);f[6]=(uint8_t)c;f[7]=(uint8_t)(c>>8);}
static void failures(void)
{
    fpga_mock_t m;fpga_mock_init(&m,8);
    fpga_link_ops_t ops=fpga_mock_ops;fpga_link_t l={.ops=&ops,.context=&m};uint8_t out[2]={0xa1,0xb2};
    ops.read=bad_read;assert(fpga_link_exchange(&l,1,0,0,out,100000)==FPGA_LINK_IO);assert(m.finishes==1&&out[0]==0xa1&&out[1]==0xb2&&!l.busy);
    ops.read=late_read;assert(fpga_link_exchange(&l,1,0,0,out,100000)==FPGA_LINK_TIMEOUT);assert(m.finishes==2&&!m.ready&&out[0]==0xa1);
    ops.read=fpga_mock_ops.read;ops.turnaround=cancel_turn;m.now=0;
    assert(fpga_link_exchange(&l,1,0,0,out,100000)==FPGA_LINK_CANCELLED);assert(m.finishes==3&&!m.ready&&out[0]==0xa1);
    m.cancel=false;ops.turnaround=fpga_mock_ops.turnaround;ops.begin=reenter;recursive=&l;
    assert(fpga_link_exchange(&l,1,0,0,out,100000)==0);assert(l.attempted==4&&l.completed==1&&l.failed==3);
    ops.begin=fpga_mock_ops.begin;l.sequence=255;
    assert(fpga_link_exchange(&l,2,11,22,out,100000)==0&&l.sequence==0);
    assert(fpga_link_exchange(&l,2,33,44,out,100000)==0&&l.sequence==1&&out[0]==33);
    unsigned before=m.finishes;
    assert(fpga_link_exchange(NULL,1,0,0,out,1)==FPGA_LINK_INVALID);
    assert(fpga_link_exchange(&l,1,0,0,NULL,1)==FPGA_LINK_INVALID);
    assert(fpga_link_exchange(&l,4,0,0,out,1)==FPGA_LINK_INVALID);
    assert(fpga_link_exchange(&l,1,0,0,out,0)==FPGA_LINK_INVALID);
    assert(fpga_link_exchange(&l,1,0,0,out,1000001)==FPGA_LINK_INVALID);
    m.now=UINT64_MAX-1;assert(fpga_link_exchange(&l,1,0,0,out,100)==FPGA_LINK_INVALID);
    assert(m.finishes==before);
    uint8_t q[8],r[8],mutated[8];fpga_link_request(q,2,55,0x55,0xaa);fpga_mock_respond(q,r);
    for(unsigned bit=0;bit<64;bit++){memcpy(mutated,r,8);mutated[bit/8]^=(uint8_t)(1u<<(bit%8));assert(fpga_link_response(mutated,q)!=0);}
    memcpy(mutated,r,8);mutated[3]=3;seal(mutated);assert(fpga_link_response(mutated,q)==FPGA_LINK_INVALID);
    memcpy(mutated,r,8);mutated[1]=3;seal(mutated);assert(fpga_link_response(mutated,q)==FPGA_LINK_INVALID);
    assert(fpga_link_response(NULL,q)==FPGA_LINK_INVALID&&fpga_link_response(r,NULL)==FPGA_LINK_INVALID);
}
static int frames(void)
{
    char line[40];
    while(fgets(line,sizeof line,stdin)) {
        uint8_t q[8],r[8];unsigned value;
        if(strcspn(line,"\r\n")!=16)return 2;
        for(unsigned i=0;i<8;i++){if(sscanf(line+i*2,"%2x",&value)!=1)return 2;q[i]=(uint8_t)value;}
        fpga_mock_respond(q,r);
        for(unsigned i=0;i<8;i++)printf("%02x",r[i]);
        printf(" %d\n",fpga_link_response(r,q)==0);
    }
    return 0;
}
int main(int argc,char**argv)
{
    if(argc==2&&!strcmp(argv[1],"--vectors"))return frames();
    if(argc>1)return fpga_cmd_main(argc-1,argv+1);
    for(unsigned chunk=1;chunk<=8;chunk++){uint32_t count,checks;assert(fpga_mock_selftest(chunk,&count,&checks)==0);assert(count==513&&checks==12);}
    failures();puts("PASS: chunks1..8, 4104 patterns, 96 injected cases, cleanup/reentry/deadlines/invalid inputs/sequence wrap/64-bit faults");return 0;
}
