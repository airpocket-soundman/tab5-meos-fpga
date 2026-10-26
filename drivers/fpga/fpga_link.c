#include <m5os/fpga_link.h>
#include <string.h>

uint16_t fpga_link_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc=0xffff;
    for (size_t n=0;n<length;n++) {
        crc^=(uint16_t)data[n]<<8;
        for (unsigned bit=0;bit<8;bit++)
            crc=(uint16_t)((crc<<1)^((crc&0x8000)?0x1021:0));
    }
    return crc;
}
void fpga_link_request(uint8_t frame[8],uint8_t op,uint8_t seq,uint8_t a,uint8_t b)
{
    frame[0]=0xa5;frame[1]=op;frame[2]=seq;frame[3]=2;frame[4]=a;frame[5]=b;
    uint16_t crc=fpga_link_crc16(frame,6);frame[6]=(uint8_t)crc;frame[7]=(uint8_t)(crc>>8);
}
int fpga_link_response(const uint8_t r[8],const uint8_t q[8])
{
    if (!r||!q) return FPGA_LINK_INVALID;
    uint16_t crc=fpga_link_crc16(r,6);
    if(r[6]!=(uint8_t)crc||r[7]!=(uint8_t)(crc>>8))return FPGA_LINK_CRC;
    if(r[0]!=0x5a||r[3]!=2||r[1]>2)return FPGA_LINK_INVALID;
    if(r[2]!=q[2])return FPGA_LINK_SEQUENCE;
    if(r[1])return FPGA_LINK_REMOTE;
    bool good=false;
    switch(q[1]) {
    case FPGA_LINK_ID:good=r[4]=='T'&&r[5]=='P';break;
    case FPGA_LINK_ECHO:good=r[4]==q[4]&&r[5]==q[5];break;
    case FPGA_LINK_INVERT:good=r[4]==(uint8_t)(q[4]^0xff)&&r[5]==(uint8_t)(q[5]^0xff);break;
    default:return FPGA_LINK_INVALID;
    }
    return good?FPGA_LINK_OK:FPGA_LINK_DATA;
}
static int checkpoint(fpga_link_t *link,uint64_t deadline)
{
    if(link->ops->cancelled&&link->ops->cancelled(link->context))return FPGA_LINK_CANCELLED;
    return link->ops->now_us(link->context)>=deadline?FPGA_LINK_TIMEOUT:FPGA_LINK_OK;
}
static int transfer(fpga_link_t *link,bool writing,uint8_t data[8],uint64_t deadline)
{
    size_t offset=0;
    for(unsigned poll=0;offset<8&&poll<FPGA_LINK_POLL_LIMIT;poll++) {
        int state=checkpoint(link,deadline);if(state)return state;
        int count=writing?link->ops->write(link->context,data+offset,8-offset,deadline)
                         :link->ops->read(link->context,data+offset,8-offset,deadline);
        if(count<0||(size_t)count>8-offset)return FPGA_LINK_IO;
        offset+=(size_t)count;
        if(!count)link->ops->idle(link->context);
    }
    return offset==8?checkpoint(link,deadline):FPGA_LINK_TIMEOUT;
}
int fpga_link_exchange(fpga_link_t *link,uint8_t op,uint8_t a,uint8_t b,uint8_t result[2],uint32_t timeout_us)
{
    if(!link||!result||!link->ops||!timeout_us||timeout_us>FPGA_LINK_TIMEOUT_MAX_US
       ||op<FPGA_LINK_ID||op>FPGA_LINK_INVERT)return FPGA_LINK_INVALID;
    const fpga_link_ops_t *ops=link->ops;
    if(!ops->begin||!ops->write||!ops->turnaround||!ops->read||!ops->finish||!ops->now_us||!ops->idle)
        return FPGA_LINK_INVALID;
    if(link->busy)return FPGA_LINK_BUSY;
    uint64_t now=ops->now_us(link->context);
    if(now>UINT64_MAX-timeout_us)return FPGA_LINK_INVALID;
    uint64_t deadline=now+timeout_us;
    uint8_t request[8],response[8];
    fpga_link_request(request,op,link->sequence++,a,b);
    link->busy=true;link->attempted++;
    int error=checkpoint(link,deadline);bool begun=false;
    if(!error){begun=true;if(ops->begin(link->context,deadline)<0)error=FPGA_LINK_IO;}
    if(!error)error=transfer(link,true,request,deadline);
    if(!error&&ops->turnaround(link->context,deadline)<0)error=FPGA_LINK_IO;
    if(!error)error=checkpoint(link,deadline);
    if(!error)error=transfer(link,false,response,deadline);
    if(!error)error=fpga_link_response(response,request);
    if(begun)ops->finish(link->context,error!=0);
    if(error)link->failed++;
    else{memcpy(result,response+4,2);link->completed++;}
    link->busy=false;return error;
}
const char *fpga_link_error(int error)
{
    switch(error){
    case 0:return "ok";case FPGA_LINK_INVALID:return "invalid";case FPGA_LINK_IO:return "io";
    case FPGA_LINK_TIMEOUT:return "timeout";case FPGA_LINK_CANCELLED:return "cancelled";
    case FPGA_LINK_CRC:return "crc";case FPGA_LINK_SEQUENCE:return "sequence";
    case FPGA_LINK_REMOTE:return "remote-error";case FPGA_LINK_DATA:return "data";
    case FPGA_LINK_BUSY:return "busy";default:return "unknown";
    }
}
