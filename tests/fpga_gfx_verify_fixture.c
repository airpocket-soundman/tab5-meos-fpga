/* Actual generic GFX service against the software endpoint; never real pins. */
#include "../drivers/fpga/fpga_gfx.h"
#include "../drivers/fpga/fpga_gfx_mock.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error Assertions required
#endif
typedef struct {
    fpga_gfx_mock_t model;
    void (*respond)(void *,const uint8_t *,uint8_t *);
    unsigned reads,clears,corrupt_read;
    uint16_t bit_planes;
} observed_t;
static void observe(void *context,const uint8_t q[8],uint8_t r[8])
{
    observed_t *o=context;
    if(q[1]==FPGA_GFX_CLEAR) {
        o->clears++;
        uint16_t color=o->model.color;
        if(color&&!(color&(color-1)))o->bit_planes|=color;
    }
    o->respond(&o->model,q,r);
    if(q[1]==FPGA_GFX_READ&&++o->reads==o->corrupt_read) {
        r[4]^=1;
        uint16_t crc=fpga_link_crc16(r,6);r[6]=(uint8_t)crc;r[7]=(uint8_t)(crc>>8);
    }
}
static void init(observed_t *o,fpga_mock_t *m,unsigned chunk)
{
    memset(o,0,sizeof *o);fpga_mock_init(m,chunk);fpga_gfx_mock_init(&o->model,m);
    o->respond=m->respond;m->respond=observe;m->respond_context=o;
}
int main(void)
{
    uint16_t pixels[FPGA_GFX_PIXELS];unsigned compared,scenes;
    for(unsigned chunk=1;chunk<=8;chunk++) {
        observed_t o;fpga_mock_t m;init(&o,&m,chunk);
        fpga_link_t l={.ops=&fpga_mock_ops,.context=&m};
        assert(!fpga_gfx_verify(&l,pixels,&compared,&scenes));
        assert(compared==FPGA_GFX_VERIFY_PIXELS&&scenes==FPGA_GFX_VERIFY_SCENES);
        assert(l.completed==22586&&o.reads==22528&&o.clears==22&&o.bit_planes==0xffff);
        for(unsigned i=0;i<FPGA_GFX_PIXELS;i++)assert(pixels[i]==0x8000);
    }
    observed_t o;fpga_mock_t m;init(&o,&m,3);
    fpga_link_t l={.ops=&fpga_mock_ops,.context=&m};
    o.corrupt_read=FPGA_GFX_PIXELS+7;
    assert(fpga_gfx_verify(&l,pixels,&compared,&scenes)==FPGA_LINK_DATA);
    assert(scenes==1&&compared==FPGA_GFX_PIXELS+6&&o.reads==FPGA_GFX_PIXELS+7&&o.clears==2);
    puts("GFX verify: 22 scenes/22528 checked pixels across chunks1..8, all RGB565 planes and first-error prefix counts passed");
    return 0;
}
