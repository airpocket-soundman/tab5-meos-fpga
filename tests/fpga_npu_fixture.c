#include "../drivers/fpga/fpga_npu.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
static unsigned ticks,cancel_at;
static bool cancel(void *p) {(void)p;return ++ticks>=cancel_at;}
static uint64_t now(void *p) {(void)p;return ticks++;}
int main(void)
{
    fpga_npu_report_t r;assert(!fpga_npu_selftest(&r,NULL));
    assert(r.requant==460&&r.dot==87&&r.gemm==12&&r.conv==8&&r.checks==13);
    fpga_npu_pair_t pairs[]={{1,1},{1,-1}};fpga_npu_quant_t q={1,0,0,-128,127};int8_t out=55;
    assert(fpga_npu_dot(pairs,2,INT32_MAX,0,&q,&out,NULL)==FPGA_NPU_OVERFLOW&&out==55);
    assert(fpga_npu_dot(NULL,1,0,0,&q,&out,NULL)==FPGA_NPU_INVALID&&out==55);
    assert(fpga_npu_dot(pairs,FPGA_NPU_MAX_SAMPLES+1,0,0,&q,&out,NULL)==FPGA_NPU_INVALID);
    fpga_npu_control_t c={cancel,NULL,NULL,0};ticks=0;cancel_at=3;
    assert(fpga_npu_dot(pairs,2,0,0,&q,&out,&c)==FPGA_NPU_CANCELLED&&out==55);
    c=(fpga_npu_control_t){NULL,now,NULL,3};ticks=0;
    assert(fpga_npu_dot(pairs,2,0,0,&q,&out,&c)==FPGA_NPU_TIMEOUT&&out==55);
    c.timeout_us=0;
    assert(fpga_npu_dot(pairs,2,0,0,&q,&out,&c)==FPGA_NPU_INVALID&&out==55);
    int8_t a[]={-128,127},b[]={-128,127};int32_t output[4]={123,123,123,123};
    assert(!fpga_npu_gemm(a,2,b,2,NULL,0,1,2,1,127,output,1,NULL));
    assert(output[0]==32640&&output[1]==123);
    output[0]=123;
    assert(fpga_npu_gemm(a,2,b,2,NULL,0,UINT_MAX,2,1,0,output,4,NULL)==FPGA_NPU_INVALID&&output[0]==123);
    assert(fpga_npu_gemm(a,2,b,2,NULL,0,1,2,1,0,output,0,NULL)==FPGA_NPU_INVALID&&output[0]==123);
    c=(fpga_npu_control_t){cancel,NULL,NULL,0};ticks=0;cancel_at=2;
    assert(fpga_npu_gemm(a,2,b,2,NULL,0,1,2,1,0,output,4,&c)==FPGA_NPU_CANCELLED&&output[0]==123);
    fpga_npu_conv_t config={1,1,1,1,1,2,2,2,2,1,1,0,0,0,0,0};unsigned h=99,w=99;
    assert(fpga_npu_conv_shape(&config,&h,&w)==FPGA_NPU_INVALID&&h==99&&w==99);
    config=(fpga_npu_conv_t){1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
    assert(!fpga_npu_conv2d(a,1,b,1,NULL,0,&config,output,1,NULL)&&output[0]==16384&&output[1]==123);
    assert(fpga_npu_conv2d(a,1,b,1,NULL,1,&config,output,1,NULL)==FPGA_NPU_INVALID);
    puts("NPU CPU: 567 carrier golden jobs; MAC fault/recovery, invalid sizes, overflow order, timeout, cancellation passed");
    return 0;
}
