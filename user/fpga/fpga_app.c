#include "fpga_app.h"
#include "../../drivers/fpga/fpga_mock.h"
#include "../../drivers/fpga/fpga_selftest.h"
#include <m5os/kprintf.h>
#include <string.h>
#if M5OS_MODULE_FPGA_PARLIO
#include "fpga_parallel_app.h"
#endif
#if M5OS_MODULE_FPGA_DIAG
#include "fpga_diag_app.h"
#endif
#if M5OS_MODULE_FPGA_NPU
#include "fpga_npu_app.h"
#endif
#if M5OS_MODULE_FPGA_GFX
#include "fpga_gfx_app.h"
#endif
#if M5OS_MODULE_FPGA
#include "../../drivers/fpga/fpga_gpio.h"
#include <m5os/hal_console.h>
static bool dock_cancel(void *ctx) {(void)ctx;return hal_console_getc()==3;}
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
            "fpga --dock dock-jumper-v0 id|test\n"
            "fpga --dock dock-jumper-v0 echo|invert BYTE BYTE\n"
#if M5OS_MODULE_FPGA_GFX
            "fpga --dock dock-jumper-v0 gfx info|test|demo (or --mock gfx ...)\n"
#endif
#if M5OS_MODULE_FPGA_NPU
            "fpga --cpu npu selftest|requant|dot\n"
#endif
#if M5OS_MODULE_FPGA_DIAG
            "fpga features\nfpga --mock suite\nfpga --dock dock-jumper-v0 suite\n"
#endif
            "Dock: check wiring/power and SW0 first. After failure reset SW0/Key_0. No DDR3/DMA.\n");
    return 1;
}
int fpga_cmd_main(int argc,char **argv)
{
#if M5OS_MODULE_FPGA_PARLIO
    if(argc>1&&!strcmp(argv[1],"--parlio"))return fpga_parallel_cmd_main(argc,argv);
#endif
#if M5OS_MODULE_FPGA_DIAG
    if(argc==2&&!strcmp(argv[1],"features"))return fpga_diag_features();
    if((argc==3&&!strcmp(argv[1],"--mock")&&!strcmp(argv[2],"suite"))
       ||(argc==4&&!strcmp(argv[1],"--dock")&&!strcmp(argv[3],"suite")))
        return fpga_diag_cmd_main(argc,argv);
#endif
#if M5OS_MODULE_FPGA_NPU
    if((argc>2&&!strcmp(argv[1],"--cpu")&&!strcmp(argv[2],"npu"))
       ||(argc>3&&!strcmp(argv[1],"--dock")&&!strcmp(argv[3],"npu")))
        return fpga_npu_cmd_main(argc,argv);
#endif
#if M5OS_MODULE_FPGA_GFX
    if((argc>2&&!strcmp(argv[1],"--mock")&&!strcmp(argv[2],"gfx"))
       ||(argc>3&&!strcmp(argv[1],"--dock")&&!strcmp(argv[3],"gfx")))
        return fpga_gfx_cmd_main(argc,argv);
#endif
#if M5OS_MODULE_FPGA
    if(argc>=2&&!strcmp(argv[1],"--dock")) {
        uint8_t op=0,a=0,b=0;
        bool test=argc==4&&!strcmp(argv[3],"test");
        if(argc==4&&!strcmp(argv[3],"id"))op=FPGA_LINK_ID;
        else if(argc==6&&(!strcmp(argv[3],"echo")||!strcmp(argv[3],"invert"))
                &&byte_arg(argv[4],&a)&&byte_arg(argv[5],&b))
            op=!strcmp(argv[3],"echo")?FPGA_LINK_ECHO:FPGA_LINK_INVERT;
        if((!op&&!test)||strcmp(argv[2],"dock-jumper-v0"))return usage();
        fpga_gpio_t gpio={0};
        int error=fpga_gpio_open(&gpio,argv[2]);
        if(error) {
            kprintf("{\"backend\":\"dock-gpio\",\"result\":\"%s\",\"transactions\":0}\n",fpga_link_error(error));
            return 1;
        }
        gpio.cancel=dock_cancel;
        fpga_link_t link={.ops=&fpga_gpio_ops,.context=&gpio};
        uint8_t result[2]={0,0};
        if(test) {
            error=fpga_link_exchange(&link,FPGA_LINK_ID,0,0,result,100000);
            for(unsigned v=0;!error&&v<256;v++) {
                error=fpga_link_exchange(&link,FPGA_LINK_ECHO,(uint8_t)v,(uint8_t)(v^255),result,100000);
                if(!error)error=fpga_link_exchange(&link,FPGA_LINK_INVERT,(uint8_t)v,(uint8_t)(v^255),result,100000);
            }
        } else error=fpga_link_exchange(&link,op,a,b,result,100000);
        if(gpio.error)error=gpio.error;
        fpga_gpio_close(&gpio);
        char txhex[17],rxhex[17];
        static const char hex[]="0123456789abcdef";
        for(unsigned i=0;i<8;i++) {
            txhex[2*i]=hex[gpio.request[i]>>4];txhex[2*i+1]=hex[gpio.request[i]&15];
            rxhex[2*i]=hex[gpio.response[i]>>4];rxhex[2*i+1]=hex[gpio.response[i]&15];
        }
        txhex[16]=rxhex[16]=0;
        kprintf("{\"backend\":\"dock-gpio\",\"profile\":\"dock-jumper-v0\",\"result\":\"%s\","
                "\"transactions\":%u,\"data\":[%u,%u],\"reset_required\":%s,"
                "\"sent\":%u,\"received\":%u,\"request_hex\":\"%s\",\"response_hex\":\"%s\",\"elapsed_us\":%u}\n",
                fpga_link_error(error),link.completed,result[0],result[1],error?"true":"false",
                gpio.sent,gpio.received,txhex,rxhex,(unsigned)gpio.elapsed_us);
        return error?1:0;
    }
#endif
    if(argc==2&&!strcmp(argv[1],"status")) {
        kprintf("{\"protocol\":\"bringup-v0\",\"backend\":\"none\",\"mock_available\":true,"
#if M5OS_MODULE_FPGA_GFX
                "\"gfx_available\":true,\"gfx_protocol\":\"dock-gfx-v0-experimental\","
#else
                "\"gfx_available\":false,"
#endif
#if M5OS_MODULE_FPGA_NPU
                "\"npu_cpu_reference\":true,\"npu_physical_backend\":false,"
#else
                "\"npu_cpu_reference\":false,\"npu_physical_backend\":false,"
#endif
#if M5OS_MODULE_FPGA
                "\"physical_backend\":true,\"profile\":\"dock-jumper-v0\","
#if M5OS_FPGA_DOCK_VERIFIED
                "\"profile_verified\":true,"
#else
                "\"profile_verified\":false,"
#endif
#else
                "\"physical_backend\":false,"
#endif
                "\"physical_link_tested\":false}\n");return 0;
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
