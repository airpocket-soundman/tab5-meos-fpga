#include "fpga_app.h"
#include "../../drivers/fpga/fpga_mock.h"
#include "../../drivers/fpga/fpga_selftest.h"
#include <m5os/kprintf.h>
#include <string.h>
static bool byte_arg(const char *text,uint8_t *out)
{
    unsigned value=0,base=10;
    if(text[0]=='0'&&(text[1]=='x'||text[1]=='X')){base=16;text+=2;}
    if(!*text)return false;
    for(;*text;text++) {
        unsigned digit=*text>='0'&&*text<='9'?(unsigned)(*text-'0'):
            *text>='a'&&*text<='f'?(unsigned)(*text-'a'+10):
            *text>='A'&&*text<='F'?(unsigned)(*text-'A'+10):256;
        if(digit>=base||value>(255-digit)/base)return false;
        value=value*base+digit;
    }
    *out=(uint8_t)value;return true;
}
static int usage(void)
{
    kprintf("fpga status\nfpga --mock id|selftest\nfpga --mock echo|invert BYTE BYTE\n"
            "Software bringup-v0 only. Physical backend, GPIO, DDR3 and DMA are unimplemented.\n");
    return 1;
}
int fpga_cmd_main(int argc,char **argv)
{
    if(argc==2&&!strcmp(argv[1],"status")) {
        kprintf("{\"protocol\":\"bringup-v0\",\"backend\":\"none\",\"mock_available\":true,"
                "\"physical_backend\":false,\"physical_link_tested\":false}\n");return 0;
    }
    if(argc<3||strcmp(argv[1],"--mock"))return usage();
    if(argc==3&&!strcmp(argv[2],"selftest")) {
        uint32_t transactions=0,checks=0;
        int result=fpga_mock_selftest(1,&transactions,&checks);
        kprintf("{\"backend\":\"mock\",\"physical_link_tested\":false,\"transactions\":%u,"
                "\"fault_checks\":%u,\"result\":\"%s\"}\n",transactions,checks,fpga_link_error(result));
        return result?1:0;
    }
    uint8_t op=0,a=0,b=0;
    if(argc==3&&!strcmp(argv[2],"id"))op=FPGA_LINK_ID;
    else if(argc==5&&(!strcmp(argv[2],"echo")||!strcmp(argv[2],"invert"))&&byte_arg(argv[3],&a)&&byte_arg(argv[4],&b))
        op=!strcmp(argv[2],"echo")?FPGA_LINK_ECHO:FPGA_LINK_INVERT;
    if(!op)return usage();
    fpga_mock_t mock;fpga_mock_init(&mock,1);
    fpga_link_t link={.ops=&fpga_mock_ops,.context=&mock};
    uint8_t result[2]={0,0};int error=fpga_link_exchange(&link,op,a,b,result,100000);
    kprintf("{\"backend\":\"mock\",\"physical_link_tested\":false,\"result\":\"%s\",\"data\":[%u,%u]}\n",
            fpga_link_error(error),result[0],result[1]);
    return error?1:0;
}
