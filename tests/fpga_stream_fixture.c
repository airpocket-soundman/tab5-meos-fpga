#include "../drivers/fpga/fpga_stream.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static uint16_t crc(const uint8_t*p,size_t n,uint16_t c){while(n--){c^=(uint16_t)*p++<<8;for(unsigned i=0;i<8;i++)c=c&0x8000?(uint16_t)((c<<1)^0x1021):(uint16_t)(c<<1);}return c;}
static void seal(uint8_t*p){uint16_t c=crc(p,6,65535);p[6]=(uint8_t)c;p[7]=(uint8_t)(c>>8);}
static void response(uint8_t*p,const uint8_t*q){p[0]=90;p[1]=0;p[2]=q[2];p[3]=2;p[4]=0;p[5]=q[5];seal(p);for(unsigned i=8;i<2056;i++)p[i]=(uint8_t)(i*37);uint16_t c=crc(p+8,2048,crc(p,6,65535));p[2056]=(uint8_t)c;p[2057]=(uint8_t)(c>>8);}
static void push(fpga_stream_t*s,const uint8_t*p){assert(fpga_stream_publish(s,s->write,p,1024));}
static void feed(fpga_stream_t*s,const uint8_t*p,unsigned n){for(unsigned i=0;i<n;i+=1024){push(s,p+i);fpga_stream_poll(s);}}
int main(void){
 fpga_stream_t*s=malloc(sizeof*s);uint8_t*raw=malloc(8192);assert(s&&raw);uint8_t q[8]={165,187,37,2,0,0};seal(q);
 for(unsigned offset=0;offset<1024;offset++){
  fpga_stream_reset(s,offset&1?0xfffff000u:0);assert(fpga_stream_begin(s,q)==LINK_STREAM_NEED_MORE);
  memset(raw,195,8192);response(raw+offset,q);feed(s,raw,4096);
  assert(!fpga_stream_body(s));assert(fpga_stream_finish(s,true)==LINK_STREAM_UNIQUE);
  assert(!memcmp(fpga_stream_body(s),raw+offset,2058));assert(s->absolute>(offset&1?UINT32_MAX:0));
 }
 /* Duplicate matches are never accepted, even when both are CRC-valid. */
 fpga_stream_reset(s,0);assert(!fpga_stream_begin(s,q));memset(raw,195,8192);response(raw+8,q);response(raw+3072,q);feed(s,raw,6144);assert(fpga_stream_finish(s,true)==LINK_STREAM_AMBIGUOUS&&!fpga_stream_body(s));
 fpga_stream_reset(s,0);assert(!fpga_stream_begin(s,q));memset(raw,195,8192);response(raw+8,q);raw[100]^=1;feed(s,raw,3072);assert(fpga_stream_finish(s,true)==LINK_STREAM_NO_MATCH&&!fpga_stream_body(s));
 /* Provisional valid candidate cannot escape failed closure or pending blocks. */
 for(unsigned fault=0;fault<3;fault++){
  fpga_stream_reset(s,0);assert(!fpga_stream_begin(s,q));memset(raw,195,8192);response(raw+8,q);feed(s,raw,3072);
  if(fault==1)push(s,raw+3072);if(fault==2)fpga_stream_poison(s);
  assert(fpga_stream_finish(s,fault!=0)==LINK_STREAM_GAP&&!fpga_stream_body(s));
 }
 /* Discard published stale blocks only, then retain late descriptor carry. */
 fpga_stream_reset(s,0xfffff000u);memset(raw,195,8192);push(s,raw);assert(!fpga_stream_begin(s,q));response(raw+8,q);feed(s,raw,3072);assert(fpga_stream_finish(s,true)==LINK_STREAM_UNIQUE);
 fpga_stream_reset(s,0);for(unsigned i=0;i<8;i++)push(s,raw);assert(!fpga_stream_publish(s,0,raw,1024)&&s->fatal);
 fpga_stream_reset(s,0);assert(!fpga_stream_publish(s,1024,raw,1024)&&s->fatal);
 fpga_stream_reset(s,0);assert(!fpga_stream_publish(s,0,raw,1023)&&s->fatal);
 fpga_stream_reset(s,1);assert(s->fatal);
 /* Body outside source5120-byte capture lease must fail even with validCRC. */
 fpga_stream_reset(s,0);assert(!fpga_stream_begin(s,q));memset(raw,195,8192);response(raw+4096,q);feed(s,raw,7168);assert(fpga_stream_finish(s,true)==LINK_STREAM_OVERFLOW&&!fpga_stream_body(s));
 q[1]=129;seal(q);fpga_stream_reset(s,0);assert(fpga_stream_begin(s,q)==LINK_STREAM_INVALID);
 free(raw);free(s);puts("PASS continuous stream:1024 offsets,32-bitwrap,uniqueCRC,ambiguity,corruption,closure/loss/pendingblocks,stalediscard,ringorder,overflow,lease,TILEonly");return 0;
}
