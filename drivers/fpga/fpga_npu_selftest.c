#include "fpga_npu.h"
#include "fpga_npu_vectors.h"
#include <limits.h>
#include <string.h>
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
static bool cancelled(const fpga_npu_control_t *c)
{return c&&c->cancelled&&c->cancelled(c->context);}
int fpga_npu_selftest_select(fpga_npu_report_t *r,const fpga_npu_control_t *control,unsigned mask)
{
    if(!r||!mask||(mask&~FPGA_NPU_TEST_ALL))return FPGA_NPU_INVALID;
    memset(r,0,sizeof *r);
    for(unsigned i=0;(mask&FPGA_NPU_TEST_REQUANT)&&i<COUNT(npu_quant_cases);i++) {
        if(cancelled(control))return FPGA_NPU_CANCELLED;
        const npu_quant_case_t *c=&npu_quant_cases[i];int8_t result=85;
        int e=fpga_npu_requantize(c->acc,&c->quant,&result);
        if(e!=c->error||result!=(e?85:c->result))return FPGA_NPU_MISMATCH;
        r->requant++;
    }
    for(unsigned i=0;(mask&FPGA_NPU_TEST_DOT)&&i<COUNT(npu_dot_cases);i++) {
        const npu_dot_case_t *c=&npu_dot_cases[i];int8_t result=85;
        int e=fpga_npu_dot(c->pairs,c->count,c->bias,c->zero,&c->quant,&result,control);
        if(e==FPGA_NPU_CANCELLED||e==FPGA_NPU_TIMEOUT)return e;
        if(e!=c->error||result!=(e?85:c->result))return FPGA_NPU_MISMATCH;
        r->dot++;
    }
    int32_t out[128];
    for(unsigned i=0;(mask&FPGA_NPU_TEST_GEMM)&&i<COUNT(npu_gemm_cases);i++) {
        const npu_gemm_case_t *c=&npu_gemm_cases[i];
        int e=fpga_npu_gemm(c->a,c->m*c->k,c->b,c->k*c->n,c->bias,c->n,
                           c->m,c->k,c->n,c->zero,out,COUNT(out),control);
        if(e==FPGA_NPU_CANCELLED||e==FPGA_NPU_TIMEOUT)return e;
        if(e!=c->error||(!e&&memcmp(out,c->expected,c->m*c->n*sizeof *out)))return FPGA_NPU_MISMATCH;
        r->gemm++;
    }
    for(unsigned i=0;(mask&FPGA_NPU_TEST_CONV)&&i<COUNT(npu_conv_cases);i++) {
        const npu_conv_case_t *c=&npu_conv_cases[i];
        int e=fpga_npu_conv2d(c->input,c->xc,c->weights,c->wc,c->bias,c->config.outputs,
                             &c->config,out,COUNT(out),control);
        if(e==FPGA_NPU_CANCELLED||e==FPGA_NPU_TIMEOUT)return e;
        if(e!=c->error||(!e&&memcmp(out,c->expected,c->count*sizeof *out)))return FPGA_NPU_MISMATCH;
        r->conv++;
    }
    if(!(mask&FPGA_NPU_TEST_MAC))return 0;
    if(cancelled(control))return FPGA_NPU_CANCELLED;
    fpga_npu_mac_t mac;int32_t result=85;
    fpga_npu_mac_reset(&mac);
#define CHECK(expression) do {if(!(expression))return FPGA_NPU_MISMATCH;r->checks++;}while(0)
    CHECK(fpga_npu_mac_end(&mac,&result)==FPGA_NPU_STATE&&result==85);
    CHECK(!fpga_npu_mac_begin(&mac,INT32_MAX));
    CHECK(fpga_npu_mac_add(&mac,1,0,1)==FPGA_NPU_OVERFLOW&&mac.accumulator==INT32_MAX);
    CHECK(fpga_npu_mac_begin(&mac,0)==FPGA_NPU_STATE);
    CHECK(fpga_npu_mac_end(&mac,&result)==FPGA_NPU_STATE&&result==85);
    fpga_npu_mac_reset(&mac);
    CHECK(!fpga_npu_mac_begin(&mac,INT32_MIN));
    CHECK(fpga_npu_mac_add(&mac,1,0,-1)==FPGA_NPU_OVERFLOW&&mac.accumulator==INT32_MIN);
    fpga_npu_mac_reset(&mac);
    CHECK(!fpga_npu_mac_begin(&mac,-17));
    CHECK(!fpga_npu_mac_end(&mac,&result)&&result==-17);
    CHECK(!fpga_npu_mac_begin(&mac,0));
    CHECK(!fpga_npu_mac_add(&mac,-128,127,-128)&&mac.accumulator==32640);
    CHECK(fpga_npu_mac_add(&mac,128,0,1)==FPGA_NPU_INVALID&&mac.accumulator==32640);
    CHECK(!fpga_npu_mac_end(&mac,&result)&&result==32640);
    return 0;
}
int fpga_npu_selftest(fpga_npu_report_t *r,const fpga_npu_control_t *c)
{return fpga_npu_selftest_select(r,c,FPGA_NPU_TEST_ALL);}
