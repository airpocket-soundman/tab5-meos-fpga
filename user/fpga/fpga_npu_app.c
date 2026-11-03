#include "fpga_npu_app.h"
#include "../../drivers/fpga/fpga_npu.h"
#include <m5os/kprintf.h>
#include <m5os/hal_time.h>
#include <string.h>
#include <limits.h>
static bool cancel(void *p) {(void)p;return kconsole_getc()==3;}
static uint64_t now(void *p) {(void)p;return hal_time_now_us();}
static bool number(const char *s,int32_t *out)
{
    bool negative=*s=='-';if(negative)s++;
    if(!*s)return false;
    uint32_t value=0,limit=negative?UINT32_C(2147483648):INT32_MAX;
    for(;*s;s++) {
        if(*s<'0'||*s>'9')return false;
        unsigned digit=(unsigned)(*s-'0');
        if(value>(limit-digit)/10)return false;
        value=value*10+digit;
    }
    *out=negative?(value==UINT32_C(2147483648)?INT32_MIN:-(int32_t)value):(int32_t)value;return true;
}
static int usage(void)
{
    kprintf("fpga --cpu npu selftest [mac|requant|dot|gemm|conv|all]\nfpga --cpu npu requant ACC MULT SHIFT ZERO LOWER UPPER\n"
            "fpga --cpu npu dot BIAS INPUT_ZERO MULT SHIFT OUTPUT_ZERO LOWER UPPER [A W ...]\n"
            "CPU arithmetic only; carrier NPU jumper commands/bitstream are not yet available.\n");
    return 1;
}
int fpga_npu_cmd_main(int argc,char **argv)
{
    if(argc>3&&!strcmp(argv[1],"--dock")&&!strcmp(argv[3],"npu")) {
        if(strcmp(argv[2],"dock-jumper-v0"))return usage();
        kprintf("{\"backend\":\"none\",\"feature\":\"npu\",\"result\":\"blocked\","
                "\"reason\":\"carrier-npu-wire-contract-and-bitstream-missing\",\"gpio_activated\":false}\n");return 2;
    }
    if(argc<4||strcmp(argv[1],"--cpu")||strcmp(argv[2],"npu"))return usage();
    fpga_npu_control_t control={cancel,now,NULL,100000};
    if((argc==4||argc==5)&&!strcmp(argv[3],"selftest")) {
        unsigned mask=FPGA_NPU_TEST_ALL;
        if(argc==5) {
            const char *names[]={"requant","dot","gemm","conv","mac","all"};
            mask=0;
            for(unsigned i=0;i<6;i++)if(!strcmp(argv[4],names[i]))mask=i==5?FPGA_NPU_TEST_ALL:1u<<i;
            if(!mask)return usage();
        }
        fpga_npu_report_t r;int e=fpga_npu_selftest_select(&r,&control,mask);
        kprintf("{\"backend\":\"cpu-reference\",\"physical_npu_tested\":false,\"result\":\"%s\","
                "\"requant_cases\":%u,\"dot_cases\":%u,\"gemm_cases\":%u,\"conv_cases\":%u,\"checks\":%u}\n",
                fpga_npu_error(e),r.requant,r.dot,r.gemm,r.conv,r.checks);return e?1:0;
    }
    int32_t a[39];
    if(argc-4>39)return usage();
    for(int i=4;i<argc;i++)if(!number(argv[i],&a[i-4]))return usage();
    int e;int8_t result=0;
    if(argc==10&&!strcmp(argv[3],"requant")) {
        fpga_npu_quant_t q={a[1],a[2],a[3],a[4],a[5]};
        e=fpga_npu_requantize(a[0],&q,&result);
    } else if(argc>=11&&!strcmp(argv[3],"dot")&&!((argc-11)%2)) {
        size_t count=(size_t)(argc-11)/2;fpga_npu_pair_t pairs[16];
        for(size_t i=0;i<count;i++) {
            int32_t act=a[7+i*2],weight=a[8+i*2];
            if(act<-128||act>127||weight<-128||weight>127)return usage();
            pairs[i]=(fpga_npu_pair_t){(int16_t)act,(int16_t)weight};
        }
        fpga_npu_quant_t q={a[2],a[3],a[4],a[5],a[6]};
        e=fpga_npu_dot(pairs,count,a[0],a[1],&q,&result,&control);
    } else return usage();
    kprintf("{\"backend\":\"cpu-reference\",\"physical_npu_tested\":false,\"result\":\"%s\",\"value\":%d}\n",fpga_npu_error(e),(int)result);
    return e?1:0;
}
