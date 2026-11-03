#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* CPU reference arithmetic only. No NPU wire ABI has been defined by carrier. */
enum { FPGA_NPU_OK=0, FPGA_NPU_INVALID=-1, FPGA_NPU_OVERFLOW=-2,
       FPGA_NPU_STATE=-3, FPGA_NPU_CANCELLED=-4, FPGA_NPU_TIMEOUT=-5, FPGA_NPU_MISMATCH=-6 };
typedef struct {unsigned requant,dot,gemm,conv,checks;} fpga_npu_report_t;
enum {FPGA_NPU_TEST_REQUANT=1,FPGA_NPU_TEST_DOT=2,FPGA_NPU_TEST_GEMM=4,
      FPGA_NPU_TEST_CONV=8,FPGA_NPU_TEST_MAC=16,FPGA_NPU_TEST_ALL=31};
#define FPGA_NPU_MAX_SAMPLES 4096u
#define FPGA_NPU_MAX_OUTPUTS 4096u
typedef struct {int32_t multiplier;int shift,zero_point,lower,upper;} fpga_npu_quant_t;
typedef struct {int16_t activation,weight;} fpga_npu_pair_t;
typedef struct {int32_t accumulator;bool active,faulted;} fpga_npu_mac_t;
typedef struct fpga_npu_control {
    bool (*cancelled)(void *context);
    uint64_t (*now_us)(void *context);
    void *context;
    uint32_t timeout_us;
} fpga_npu_control_t;
int fpga_npu_selftest(fpga_npu_report_t *report,const fpga_npu_control_t *control);
int fpga_npu_selftest_select(fpga_npu_report_t *report,const fpga_npu_control_t *control,unsigned mask);
void fpga_npu_mac_reset(fpga_npu_mac_t *mac);
int fpga_npu_mac_begin(fpga_npu_mac_t *mac,int32_t bias);
int fpga_npu_mac_add(fpga_npu_mac_t *mac,int activation,int zero_point,int weight);
int fpga_npu_mac_end(fpga_npu_mac_t *mac,int32_t *result);
int fpga_npu_requantize(int32_t accumulator,const fpga_npu_quant_t *quant,int8_t *result);
int fpga_npu_dot(const fpga_npu_pair_t *pairs,size_t count,int32_t bias,int input_zero,
                 const fpga_npu_quant_t *quant,int8_t *result,const fpga_npu_control_t *control);
/* Row-major MxK * KxN. M,N<=16, K<=256. Optional per-output-channel bias.
 * Invalid arguments do not alter output. Runtime failure can leave partial
 * matrix/tensor output; callers must discard the entire result on failure. */
int fpga_npu_gemm(const int8_t *a,size_t a_count,const int8_t *b,size_t b_count,
                  const int32_t *bias,size_t bias_count,unsigned m,unsigned k,unsigned n,
                  int input_zero,int32_t *out,size_t capacity,const fpga_npu_control_t *control);
typedef struct {
    unsigned batch,channels,height,width,outputs,kh,kw;
    unsigned stride_y,stride_x,dilation_y,dilation_x;
    unsigned pad_top,pad_left,pad_bottom,pad_right;
    int input_zero;
} fpga_npu_conv_t;
/* NCHW input, OIHW weights, group=1. batch<=4, channels/outputs<=8,
 * spatial<=16, kernel<=5, stride/dilation<=4, padding<=8, output<=4096. */
int fpga_npu_conv_shape(const fpga_npu_conv_t *config,unsigned *height,unsigned *width);
int fpga_npu_conv2d(const int8_t *x,size_t x_count,const int8_t *weights,size_t w_count,
                    const int32_t *bias,size_t bias_count,const fpga_npu_conv_t *config,
                    int32_t *out,size_t capacity,const fpga_npu_control_t *control);
const char *fpga_npu_error(int error);
