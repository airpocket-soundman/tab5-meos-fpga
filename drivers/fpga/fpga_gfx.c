#include "fpga_gfx.h"
static int rpc(fpga_link_t *l,uint8_t op,uint8_t a,uint8_t b)
{
    uint8_t result[2];return fpga_link_exchange(l,op,a,b,result,100000);
}
int fpga_gfx_info(fpga_link_t *l) {return rpc(l,FPGA_GFX_INFO,0,0);}
int fpga_gfx_color(fpga_link_t *l,uint16_t c)
{return rpc(l,FPGA_GFX_COLOR,(uint8_t)c,(uint8_t)(c>>8));}
int fpga_gfx_clear(fpga_link_t *l,uint16_t c)
{
    int e=fpga_gfx_color(l,c);return e?e:rpc(l,FPGA_GFX_CLEAR,0,0);
}
int fpga_gfx_pixel(fpga_link_t *l,unsigned x,unsigned y,uint16_t c)
{
    if(x>=32||y>=32)return FPGA_LINK_RANGE;
    int e=fpga_gfx_color(l,c);return e?e:rpc(l,FPGA_GFX_PIXEL,(uint8_t)x,(uint8_t)y);
}
int fpga_gfx_rect(fpga_link_t *l,unsigned x,unsigned y,unsigned w,unsigned h,uint16_t c)
{
    if(x>=32||y>=32||!w||!h||w>32-x||h>32-y)return FPGA_LINK_RANGE;
    int e=fpga_gfx_color(l,c);
    if(!e)e=rpc(l,FPGA_GFX_ORIGIN,(uint8_t)x,(uint8_t)y);
    return e?e:rpc(l,FPGA_GFX_RECT,(uint8_t)w,(uint8_t)h);
}
int fpga_gfx_read(fpga_link_t *l,unsigned x,unsigned y,uint16_t *c)
{
    if(!c)return FPGA_LINK_INVALID;
    if(x>=32||y>=32)return FPGA_LINK_RANGE;
    uint8_t r[2];int e=fpga_link_exchange(l,FPGA_GFX_READ,(uint8_t)x,(uint8_t)y,r,100000);
    if(!e)*c=(uint16_t)(r[0]|((uint16_t)r[1]<<8));
    return e;
}
int fpga_gfx_diagnostic(fpga_link_t *l,uint16_t pixels[FPGA_GFX_PIXELS],unsigned *compared)
{
    if(!pixels||!compared)return FPGA_LINK_INVALID;
    *compared=0;
    int e=rpc(l,FPGA_LINK_ID,0,0);
    if(!e)e=fpga_gfx_info(l);
    if(!e)e=fpga_gfx_clear(l,0x0841);
    if(!e)e=fpga_gfx_rect(l,1,2,9,5,0xf800);
    if(!e)e=fpga_gfx_rect(l,19,7,4,13,0x07e0);
    if(!e)e=fpga_gfx_pixel(l,31,0,0x001f);
    if(!e)e=fpga_gfx_pixel(l,0,31,0xffe0);
    if(!e)e=fpga_gfx_pixel(l,30,29,0xf81f);
    for(unsigned y=0;!e&&y<32;y++)for(unsigned x=0;!e&&x<32;x++) {
        unsigned i=y*32+x;
        e=fpga_gfx_read(l,x,y,&pixels[i]);
        uint16_t expected=0x0841;
        if(x>=1&&x<10&&y>=2&&y<7)expected=0xf800;
        if(x>=19&&x<23&&y>=7&&y<20)expected=0x07e0;
        if(x==31&&y==0)expected=0x001f;
        if(x==0&&y==31)expected=0xffe0;
        if(x==30&&y==29)expected=0xf81f;
        if(!e&&pixels[i]!=expected)e=FPGA_LINK_DATA;
        if(!e)(*compared)++;
    }
    return e;
}
