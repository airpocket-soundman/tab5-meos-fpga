#include "fpga_diag_app.h"
#include "../../drivers/fpga/fpga_gpio.h"
#include "../../drivers/fpga/fpga_gfx.h"
#include "../../drivers/fpga/fpga_gfx_mock.h"
#include "../../drivers/fpga/fpga_npu.h"
#include <m5os/kmalloc.h>
#include <m5os/kprintf.h>
#include <m5os/hal_time.h>
#include <string.h>
static bool cancel(void *p) {(void)p;return kconsole_getc()==3;}
static uint64_t now(void *p) {(void)p;return hal_time_now_us();}
int fpga_diag_features(void)
{
    kprintf("{\"scope\":\"host-implementation-inventory-not-device-probe\","
            "\"profile\":\"dock-jumper-v0\",\"link\":\"gpio-and-mock\",\"gfx_32x32\":\"gpio-and-mock\","
            "\"npu_arithmetic\":\"cpu-reference\",\"cpu_gfxbench\":true,"
            "\"physical_npu\":\"carrier-contract-and-bitstream-missing\","
            "\"ddr3_bist\":\"unavailable\",\"ddr3_read_write\":\"unavailable\","
            "\"ramdisk\":\"unavailable\",\"hdmi_host_test\":\"unavailable\","
            "\"polygon_offload\":\"unavailable\",\"blit_glyph\":\"unavailable\","
            "\"parlio_dma\":\"unavailable\",\"full_hardware_coverage\":false}\n");return 0;
}
int fpga_diag_cmd_main(int argc,char **argv)
{
    bool mock=argc==3&&!strcmp(argv[1],"--mock")&&!strcmp(argv[2],"suite");
    bool dock=argc==4&&!strcmp(argv[1],"--dock")&&!strcmp(argv[2],"dock-jumper-v0")&&!strcmp(argv[3],"suite");
    if(!mock&&!dock)return 1;
    fpga_gfx_mock_t *model=mock?kmalloc(sizeof *model):NULL;
    uint16_t *pixels=kmalloc(FPGA_GFX_PIXELS*sizeof *pixels);
    if((mock&&!model)||!pixels){kfree(model);kfree(pixels);kprintf("fpga suite: no memory\n");return 1;}
    fpga_mock_t transport;fpga_gpio_t gpio={0};fpga_link_t link={0};
    int error=0;
    if(mock) {
        fpga_mock_init(&transport,1);fpga_gfx_mock_init(model,&transport);
        link.ops=&fpga_mock_ops;link.context=&transport;
    } else {
        error=fpga_gpio_open(&gpio,"dock-jumper-v0");gpio.cancel=cancel;
        link.ops=&fpga_gpio_ops;link.context=&gpio;
    }
    uint8_t result[2];unsigned link_tests=0,compared=0;
    if(!error) {error=fpga_link_exchange(&link,FPGA_LINK_ID,0,0,result,100000);if(!error)link_tests++;}
    for(unsigned v=0;!error&&v<256;v++)for(unsigned op=FPGA_LINK_ECHO;!error&&op<=FPGA_LINK_INVERT;op++) {
        error=fpga_link_exchange(&link,(uint8_t)op,(uint8_t)v,(uint8_t)(v^255),result,100000);
        if(!error)link_tests++;
    }
    if(!error)error=fpga_gfx_diagnostic(&link,pixels,&compared);
    if(dock){if(gpio.error)error=gpio.error;fpga_gpio_close(&gpio);}
    kfree(pixels);kfree(model);
    fpga_npu_report_t npu={0};fpga_npu_control_t control={cancel,now,NULL,100000};
    /* Once communication fails, stop the run. Do not obscure it with CPU work. */
    int npu_error=error?FPGA_NPU_STATE:fpga_npu_selftest(&npu,&control);
    kprintf("{\"backend\":\"%s\",\"result\":\"%s\",\"link_result\":\"%s\","
            "\"link_exchanges\":%u,\"gfx_pixels_compared\":%u,\"npu_backend\":\"cpu-reference\","
            "\"npu_result\":\"%s\",\"npu_cases\":%u,\"npu_ran\":%s,"
            "\"full_hardware_coverage\":false,\"reset_required\":%s,"
            "\"blocked\":[\"physical-npu\",\"ddr3-bist\",\"ddr3-read-write\",\"ramdisk\",\"hdmi\",\"polygon-offload\",\"blit-glyph\",\"parlio-dma\"]}\n",
            mock?"mock":"dock-gpio",error||npu_error?"failed":mock?"ok-software":"partial",fpga_link_error(error),
            link_tests,compared,fpga_npu_error(npu_error),npu.requant+npu.dot+npu.gemm+npu.conv,
            error?"false":"true",dock&&link.attempted&&error?"true":"false");
    return error||npu_error?1:mock?0:2;
}
