#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../drivers/usb_dma/uvc_protocol.h"
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);uint8_t b[4096];size_t n=fread(b,1,sizeof b,f);fclose(f);assert(n==1037);
 uvc_descriptor_t v;assert(uvc_descriptors(b,n,&v));assert(v.version==0x100&&v.nmodes==10&&v.nalts==7&&v.ep==0x82);
 bool small=false;for(unsigned i=0;i<v.nmodes;i++)if(v.modes[i].w==160&&v.modes[i].h==120){assert(v.modes[i].format==1&&v.modes[i].index==5);small=true;}assert(small);
 for(size_t i=0;i<n;i++)assert(!uvc_descriptors(b,i,&v));
 uint32_t rng=1;for(unsigned round=0;round<20000;round++){uint8_t copy[4096];memcpy(copy,b,n);rng=rng*1664525u+1013904223u;size_t at=rng%n;copy[at]^=1u<<(rng>>29);if(uvc_descriptors(copy,n,&v)){assert(v.nmodes<=UVC_MAX_MODES&&v.nalts<=UVC_MAX_ALTS);}}
 uint8_t raw[4]={0};uvc_frame_t frame={.data=raw,.capacity=sizeof raw};
 uint8_t a[]={2,0x80,1,2},z[]={2,0x82,3,4};assert(!uvc_payload(&frame,a,sizeof a));assert(uvc_payload(&frame,z,sizeof z));assert(!memcmp(raw,"\1\2\3\4",4));
 uint8_t oversize[]={2,0x83,1,2,3,4,5};assert(!uvc_payload(&frame,oversize,sizeof oversize)&&frame.bad);
 uint8_t good[]={2,0x82,9,8,7,6};assert(uvc_payload(&frame,good,sizeof good));
 uint8_t bad[]={2,0xc3,1,2,3,4};assert(!uvc_payload(&frame,bad,sizeof bad)&&frame.bad);
 uint8_t shortpts[]={2,0x86};assert(!uvc_payload(&frame,shortpts,sizeof shortpts)&&frame.bad);
 puts("PASS UVC real descriptors, truncation/mutation bounds, frame assembly/error/FID/overflow");return 0;
}
