#include "fpga_npu.h"
#include <limits.h>
static bool i8(int v) {return v>=-128&&v<=127;}
static bool quant_valid(const fpga_npu_quant_t *q)
{
    return q&&q->multiplier>=0&&q->shift>=0&&q->shift<=62&&i8(q->zero_point)
        &&i8(q->lower)&&i8(q->upper)&&q->lower<=q->upper;
}
static int accumulate(int32_t *acc,int a,int zero,int weight)
{
    int64_t sum=(int64_t)*acc+(int64_t)(a-zero)*weight;
    if(sum<INT32_MIN||sum>INT32_MAX)return FPGA_NPU_OVERFLOW;
    *acc=(int32_t)sum;return 0;
}
void fpga_npu_mac_reset(fpga_npu_mac_t *m)
{if(m)*m=(fpga_npu_mac_t){0};}
int fpga_npu_mac_begin(fpga_npu_mac_t *m,int32_t bias)
{
    if(!m)return FPGA_NPU_INVALID;
    if(m->active||m->faulted)return FPGA_NPU_STATE;
    m->accumulator=bias;m->active=true;return 0;
}
int fpga_npu_mac_add(fpga_npu_mac_t *m,int a,int zero,int weight)
{
    if(!m)return FPGA_NPU_INVALID;
    if(!m->active||m->faulted)return FPGA_NPU_STATE;
    if(!i8(a)||!i8(zero)||!i8(weight))return FPGA_NPU_INVALID;
    int e=accumulate(&m->accumulator,a,zero,weight);
    if(e){m->faulted=true;m->active=false;}
    return e;
}
int fpga_npu_mac_end(fpga_npu_mac_t *m,int32_t *result)
{
    if(!m||!result)return FPGA_NPU_INVALID;
    if(!m->active||m->faulted)return FPGA_NPU_STATE;
    *result=m->accumulator;m->active=false;return 0;
}
int fpga_npu_requantize(int32_t acc,const fpga_npu_quant_t *q,int8_t *result)
{
    if(!result||!quant_valid(q))return FPGA_NPU_INVALID;
    int64_t product=(int64_t)acc*q->multiplier;
    uint64_t magnitude=(uint64_t)(product<0?-product:product);
    if(q->shift)magnitude+=(UINT64_C(1)<<(q->shift-1));
    magnitude>>=q->shift;
    int64_t rounded=(product<0?-(int64_t)magnitude:(int64_t)magnitude)+q->zero_point;
    if(rounded<q->lower)rounded=q->lower;
    if(rounded>q->upper)rounded=q->upper;
    *result=(int8_t)rounded;return 0;
}
typedef struct {const fpga_npu_control_t *control;uint64_t end;} guard_t;
static int guard_check(const guard_t *g)
{
    const fpga_npu_control_t *c=g->control;
    if(!c)return 0;
    if(c->cancelled&&c->cancelled(c->context))return FPGA_NPU_CANCELLED;
    if(c->now_us&&c->now_us(c->context)>=g->end)return FPGA_NPU_TIMEOUT;
    return 0;
}
static int guard_init(guard_t *g,const fpga_npu_control_t *c)
{
    *g=(guard_t){.control=c};
    if(c&&c->now_us) {
        uint64_t now=c->now_us(c->context);
        if(!c->timeout_us||c->timeout_us>1000000||now>UINT64_MAX-c->timeout_us)return FPGA_NPU_INVALID;
        g->end=now+c->timeout_us;
    }
    return guard_check(g);
}
int fpga_npu_dot(const fpga_npu_pair_t *pairs,size_t count,int32_t bias,int zero,
                 const fpga_npu_quant_t *q,int8_t *result,const fpga_npu_control_t *control)
{
    if(!result||!quant_valid(q)||!i8(zero)||count>FPGA_NPU_MAX_SAMPLES||(!pairs&&count))return FPGA_NPU_INVALID;
    for(size_t i=0;i<count;i++)if(!i8(pairs[i].activation)||!i8(pairs[i].weight))return FPGA_NPU_INVALID;
    guard_t g;int e=guard_init(&g,control);if(e)return e;
    int32_t acc=bias;
    for(size_t i=0;i<count;i++) {
        if((e=guard_check(&g)))return e;
        if((e=accumulate(&acc,pairs[i].activation,zero,pairs[i].weight)))return e;
    }
    if((e=guard_check(&g)))return e;
    return fpga_npu_requantize(acc,q,result);
}
int fpga_npu_gemm(const int8_t *a,size_t ac,const int8_t *b,size_t bc,
                  const int32_t *bias,size_t bias_count,unsigned m,unsigned k,unsigned n,
                  int zero,int32_t *out,size_t capacity,const fpga_npu_control_t *control)
{
    if(!a||!b||!out||!m||m>16||!k||k>256||!n||n>16||!i8(zero)
       ||ac!=(size_t)m*k||bc!=(size_t)k*n||capacity<(size_t)m*n
       ||(bias?bias_count!=n:bias_count!=0))return FPGA_NPU_INVALID;
    guard_t g;int e=guard_init(&g,control);if(e)return e;
    for(unsigned i=0;i<m;i++)for(unsigned j=0;j<n;j++) {
        int32_t acc=bias?bias[j]:0;
        for(unsigned p=0;p<k;p++) {
            if((e=guard_check(&g)))return e;
            if((e=accumulate(&acc,a[i*k+p],zero,b[p*n+j])))return e;
        }
        out[i*n+j]=acc;
    }
    return guard_check(&g);
}
int fpga_npu_conv_shape(const fpga_npu_conv_t *c,unsigned *oh,unsigned *ow)
{
    if(!c||!oh||!ow||!c->batch||c->batch>4||!c->channels||c->channels>8
       ||!c->outputs||c->outputs>8||!c->height||c->height>16||!c->width||c->width>16
       ||!c->kh||c->kh>5||!c->kw||c->kw>5||!c->stride_y||c->stride_y>4
       ||!c->stride_x||c->stride_x>4||!c->dilation_y||c->dilation_y>4
       ||!c->dilation_x||c->dilation_x>4||c->pad_top>8||c->pad_left>8
       ||c->pad_bottom>8||c->pad_right>8||!i8(c->input_zero))return FPGA_NPU_INVALID;
    int h=(int)(c->height+c->pad_top+c->pad_bottom)-(int)(c->dilation_y*(c->kh-1))-1;
    int w=(int)(c->width+c->pad_left+c->pad_right)-(int)(c->dilation_x*(c->kw-1))-1;
    if(h<0||w<0)return FPGA_NPU_INVALID;
    unsigned y=(unsigned)h/c->stride_y+1,x=(unsigned)w/c->stride_x+1;
    if((size_t)c->batch*c->outputs*y*x>FPGA_NPU_MAX_OUTPUTS)return FPGA_NPU_INVALID;
    *oh=y;*ow=x;return 0;
}
int fpga_npu_conv2d(const int8_t *input,size_t xc,const int8_t *weights,size_t wc,
                    const int32_t *bias,size_t bias_count,const fpga_npu_conv_t *c,
                    int32_t *out,size_t capacity,const fpga_npu_control_t *control)
{
    unsigned oh,ow;
    if(fpga_npu_conv_shape(c,&oh,&ow)||!input||!weights||!out
       ||xc!=(size_t)c->batch*c->channels*c->height*c->width
       ||wc!=(size_t)c->outputs*c->channels*c->kh*c->kw
       ||capacity<(size_t)c->batch*c->outputs*oh*ow
       ||(bias?bias_count!=c->outputs:bias_count!=0))return FPGA_NPU_INVALID;
    guard_t g;int e=guard_init(&g,control);if(e)return e;
    for(unsigned n=0;n<c->batch;n++)for(unsigned o=0;o<c->outputs;o++)
    for(unsigned y=0;y<oh;y++)for(unsigned x=0;x<ow;x++) {
        int32_t acc=bias?bias[o]:0;
        for(unsigned ch=0;ch<c->channels;ch++)for(unsigned ky=0;ky<c->kh;ky++)for(unsigned kx=0;kx<c->kw;kx++) {
            if((e=guard_check(&g)))return e;
            int iy=(int)(y*c->stride_y+ky*c->dilation_y)-(int)c->pad_top;
            int ix=(int)(x*c->stride_x+kx*c->dilation_x)-(int)c->pad_left;
            int a=c->input_zero;
            if(iy>=0&&ix>=0&&(unsigned)iy<c->height&&(unsigned)ix<c->width)
                a=input[((n*c->channels+ch)*c->height+(unsigned)iy)*c->width+(unsigned)ix];
            int weight=weights[((o*c->channels+ch)*c->kh+ky)*c->kw+kx];
            if((e=accumulate(&acc,a,c->input_zero,weight)))return e;
        }
        out[((n*c->outputs+o)*oh+y)*ow+x]=acc;
    }
    return guard_check(&g);
}
const char *fpga_npu_error(int e)
{
    switch(e){case 0:return "ok";case FPGA_NPU_INVALID:return "invalid-param";
    case FPGA_NPU_OVERFLOW:return "acc-overflow";case FPGA_NPU_STATE:return "state";
    case FPGA_NPU_CANCELLED:return "cancelled";case FPGA_NPU_TIMEOUT:return "timeout";
    case FPGA_NPU_MISMATCH:return "mismatch";default:return "unknown";}
}
