#include "fpga_mock.h"
#include <string.h>
static void seal(uint8_t f[8]){uint16_t c=fpga_link_crc16(f,6);f[6]=(uint8_t)c;f[7]=(uint8_t)(c>>8);}
void fpga_mock_respond(const uint8_t q[8],uint8_t r[8])
{
    memset(r,0,8);r[0]=0x5a;r[2]=q[2];r[3]=2;
    uint16_t c=fpga_link_crc16(q,6);
    if(q[0]!=0xa5||q[3]!=2||q[6]!=(uint8_t)c||q[7]!=(uint8_t)(c>>8))r[1]=1;
    else switch(q[1]){
    case FPGA_LINK_ID:r[4]='T';r[5]='P';break;
    case FPGA_LINK_ECHO:r[4]=q[4];r[5]=q[5];break;
    case FPGA_LINK_INVERT:r[4]=(uint8_t)~q[4];r[5]=(uint8_t)~q[5];break;
    default:r[1]=2;break;
    }
    seal(r);
}
void fpga_mock_init(fpga_mock_t*m,unsigned chunk){memset(m,0,sizeof(*m));m->chunk=chunk>=1&&chunk<=8?chunk:8;}
static int begin(void*p,uint64_t d){(void)d;fpga_mock_t*m=p;m->written=m->read=0;m->ready=false;return m->fault==FPGA_MOCK_BEGIN_FAIL?-1:0;}
static int write_bytes(void*p,const uint8_t*data,size_t n,uint64_t d)
{
    (void)d;fpga_mock_t*m=p;
    if(m->fault==FPGA_MOCK_STALL_WRITE)return 0;
    if(m->fault==FPGA_MOCK_IO)return -1;
    if(m->fault==FPGA_MOCK_OVERSIZE)return (int)n+1;
    if(n>m->chunk)n=m->chunk;
    if(m->written+n>8||m->ready)return -1;
    memcpy(m->request+m->written,data,n);m->written+=(unsigned)n;return (int)n;
}
static int turnaround(void*p,uint64_t d)
{
    (void)d;fpga_mock_t*m=p;if(m->written!=8||m->fault==FPGA_MOCK_TURN_FAIL)return -1;
    fpga_mock_respond(m->request,m->response);
    if(m->fault==FPGA_MOCK_BAD_CRC)m->response[6]^=1;
    if(m->fault==FPGA_MOCK_OLD_SEQUENCE){m->response[2]--;seal(m->response);}
    if(m->fault==FPGA_MOCK_WRONG_DATA){m->response[4]^=1;seal(m->response);}
    m->ready=true;return 0;
}
static int read_bytes(void*p,uint8_t*data,size_t n,uint64_t d)
{
    (void)d;fpga_mock_t*m=p;if(!m->ready)return -1;
    if(m->fault==FPGA_MOCK_STALL_READ)return 0;
    unsigned limit=m->fault==FPGA_MOCK_TRUNCATE?7:8;
    if(m->read>=limit)return 0;
    if(n>m->chunk)n=m->chunk;
    if(n>limit-m->read)n=limit-m->read;
    memcpy(data,m->response+m->read,n);m->read+=(unsigned)n;return (int)n;
}
static void finish(void*p,bool failed){fpga_mock_t*m=p;m->finishes++;m->last_failed=failed;m->written=m->read=0;m->ready=false;}
static uint64_t clock_us(void*p){return ((fpga_mock_t*)p)->now;}
static void idle(void*p){fpga_mock_t*m=p;if(!m->frozen_clock)m->now+=1000;}
static bool cancelled(void*p){return ((fpga_mock_t*)p)->cancel;}
const fpga_link_ops_t fpga_mock_ops={begin,write_bytes,turnaround,read_bytes,finish,clock_us,idle,cancelled};
