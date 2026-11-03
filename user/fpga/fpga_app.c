#include "fpga_app.h"
#include "../../drivers/fpga/fpga_mock.h"
#include "../../drivers/fpga/fpga_selftest.h"
#include <m5os/kprintf.h>
#include <string.h>
#if defined(__riscv) && M5OS_BOARD_TAB5
#include "../../drivers/fpga/fpga_gpio.h"
#define FPGA_NATIVE_GPIO 1
#endif
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
            "Dock bench: fpga --gpio --arm dock-v0 id|test (unverified profile rejects output). DDR3/DMA unimplemented.\n");
    return 1;
}
#ifdef FPGA_NATIVE_GPIO
static int gpio_command(int argc,char **argv)
{
    if(argc!=5||strcmp(argv[2],"--arm")||strcmp(argv[3],"dock-v0"))return usage();
    unsigned count=!strcmp(argv[4],"test")?513:!strcmp(argv[4],"id")?1:0;
    if(!count)return usage();
    fpga_link_t link={.ops=&fpga_gpio_ops};
    unsigned completed=0;int error=0;uint8_t data[2]={0,0};
    for(unsigned i=0;i<count;i++) {
        fpga_gpio_t bus={0};
        error=fpga_gpio_open(&bus,true);
        if(error)break;
        link.context=&bus;
        uint8_t op=i==0?FPGA_LINK_ID:(i&1)?FPGA_LINK_ECHO:FPGA_LINK_INVERT;
        uint8_t value=(uint8_t)((i-1)/2);
        error=fpga_link_exchange(&link,op,i?value:0,i?(uint8_t)(value^0xa5):0,data,100000);
        fpga_gpio_close(&bus);
        if(error)break;
        completed++;
    }
    kprintf("{\"backend\":\"dock-gpio\",\"profile_verified\":%s,\"armed\":false,"
            "\"completed\":%u,\"expected\":%u,\"result\":\"%s\",\"data\":[%u,%u]}\n",
            M5OS_FPGA_DOCK_VERIFIED?"true":"false",completed,count,fpga_link_error(error),data[0],data[1]);
    return error?1:0;
}
#endif
int fpga_cmd_main(int argc,char **argv)
{
#ifdef FPGA_NATIVE_GPIO
    if(argc>=2&&!strcmp(argv[1],"--gpio"))return gpio_command(argc,argv);
#endif
    if(argc==2&&!strcmp(argv[1],"status")) {
#ifdef FPGA_NATIVE_GPIO
        kprintf("{\"protocol\":\"bringup-v0\",\"backend\":\"none\",\"mock_available\":true,"
                "\"physical_backend\":true,\"physical_link_tested\":false,\"profile\":\"dock-v0\","
                "\"profile_verified\":%s,\"armed\":false,\"max_clock_hz\":5000}\n",
                M5OS_FPGA_DOCK_VERIFIED?"true":"false");return 0;
#endif
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
