#include "fpga_gfx_mock.h"
#include <string.h>
/* Software state model of the carrier draft; no RTL or DDR3 here. */
static void respond(void *ctx,const uint8_t q[8],uint8_t r[8])
{
    fpga_gfx_mock_t *g=ctx;
    fpga_mock_respond(q,r);
    uint16_t crc=fpga_link_crc16(q,6);
    if(q[0]!=0xa5||q[3]!=2||q[6]!=(uint8_t)crc||q[7]!=(uint8_t)(crc>>8)
       ||q[1]<FPGA_GFX_INFO||q[1]>FPGA_GFX_RECT)return;
    unsigned a=q[4],b=q[5];r[1]=0;r[4]=q[4];r[5]=q[5];
    switch(q[1]) {
    case FPGA_GFX_INFO:r[4]=32;r[5]=32;break;
    case FPGA_GFX_COLOR:g->color=(uint16_t)(a|(b<<8));break;
    case FPGA_GFX_CLEAR:
        for(unsigned i=0;i<FPGA_GFX_PIXELS;i++)g->pixels[i]=g->color;
        g->initialized=true;g->x=g->y=0;r[4]=r[5]=0;break;
    case FPGA_GFX_PIXEL:case FPGA_GFX_READ:case FPGA_GFX_ORIGIN:
        if(a>=32||b>=32)r[1]=3;
        else if(q[1]==FPGA_GFX_ORIGIN){g->x=a;g->y=b;}
        else if(!g->initialized)r[1]=5;
        else if(q[1]==FPGA_GFX_PIXEL)g->pixels[b*32+a]=g->color;
        else {uint16_t c=g->pixels[b*32+a];r[4]=(uint8_t)c;r[5]=(uint8_t)(c>>8);}
        break;
    case FPGA_GFX_RECT:
        if(!a||!b||a>32-g->x||b>32-g->y)r[1]=3;
        else if(!g->initialized)r[1]=5;
        else for(unsigned y=g->y;y<g->y+b;y++)for(unsigned x=g->x;x<g->x+a;x++)
            g->pixels[y*32+x]=g->color;
        break;
    }
    if(r[1])r[4]=r[5]=0;
    crc=fpga_link_crc16(r,6);r[6]=(uint8_t)crc;r[7]=(uint8_t)(crc>>8);
}
void fpga_gfx_mock_init(fpga_gfx_mock_t *g,fpga_mock_t *m)
{
    memset(g,0,sizeof *g);g->color=0xffff;
    m->respond=respond;m->respond_context=g;
}
