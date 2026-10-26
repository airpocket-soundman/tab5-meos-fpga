#include "fpga_selftest.h"
#include "fpga_mock.h"
int fpga_mock_selftest(unsigned chunk,uint32_t *transactions,uint32_t *fault_checks)
{
    if(!transactions||!fault_checks||chunk<1||chunk>8)return FPGA_LINK_INVALID;
    *transactions=*fault_checks=0;
    fpga_mock_t mock;fpga_mock_init(&mock,chunk);
    fpga_link_t link={.ops=&fpga_mock_ops,.context=&mock};
    uint8_t value[2];
    if(fpga_link_crc16((const uint8_t*)"123456789",9)!=0x29b1)return FPGA_LINK_CRC;
    int result=fpga_link_exchange(&link,FPGA_LINK_ID,0,0,value,100000);
    if(result||value[0]!='T'||value[1]!='P')return FPGA_LINK_DATA;
    for(unsigned i=0;i<256;i++) {
        if(fpga_link_exchange(&link,FPGA_LINK_ECHO,(uint8_t)i,(uint8_t)(i^0xa5),value,100000)
           ||value[0]!=i||value[1]!=(uint8_t)(i^0xa5))return FPGA_LINK_DATA;
        if(fpga_link_exchange(&link,FPGA_LINK_INVERT,(uint8_t)i,(uint8_t)(255-i),value,100000)
           ||value[0]!=(uint8_t)(255-i)||value[1]!=i)return FPGA_LINK_DATA;
    }
    *transactions=link.completed;
    const struct {unsigned fault;int expected;} faults[]={
        {FPGA_MOCK_STALL_WRITE,FPGA_LINK_TIMEOUT},{FPGA_MOCK_STALL_READ,FPGA_LINK_TIMEOUT},
        {FPGA_MOCK_BAD_CRC,FPGA_LINK_CRC},{FPGA_MOCK_OLD_SEQUENCE,FPGA_LINK_SEQUENCE},
        {FPGA_MOCK_WRONG_DATA,FPGA_LINK_DATA},{FPGA_MOCK_TRUNCATE,FPGA_LINK_TIMEOUT},
        {FPGA_MOCK_IO,FPGA_LINK_IO},{FPGA_MOCK_OVERSIZE,FPGA_LINK_IO},
        {FPGA_MOCK_BEGIN_FAIL,FPGA_LINK_IO},{FPGA_MOCK_TURN_FAIL,FPGA_LINK_IO}};
    for(unsigned i=0;i<sizeof(faults)/sizeof(faults[0]);i++) {
        mock.fault=faults[i].fault;
        value[0]=0xa1;value[1]=0xb2;unsigned previous=mock.finishes;
        result=fpga_link_exchange(&link,FPGA_LINK_ECHO,0x55,0xaa,value,100000);
        if(result!=faults[i].expected||value[0]!=0xa1||value[1]!=0xb2||link.busy
           ||mock.finishes!=previous+1||!mock.last_failed||mock.ready)return FPGA_LINK_DATA;
        (*fault_checks)++;
        mock.fault=FPGA_MOCK_NORMAL;
        if(fpga_link_exchange(&link,FPGA_LINK_ID,0,0,value,100000))return FPGA_LINK_DATA;
    }
    mock.cancel=true;unsigned previous=mock.finishes;
    if(fpga_link_exchange(&link,FPGA_LINK_ID,0,0,value,100000)!=FPGA_LINK_CANCELLED
       ||mock.finishes!=previous||link.busy)return FPGA_LINK_DATA;
    (*fault_checks)++;mock.cancel=false;
    mock.fault=FPGA_MOCK_STALL_READ;mock.frozen_clock=true;
    if(fpga_link_exchange(&link,FPGA_LINK_ID,0,0,value,100000)!=FPGA_LINK_TIMEOUT||mock.ready||link.busy)
        return FPGA_LINK_DATA;
    (*fault_checks)++;
    return FPGA_LINK_OK;
}
