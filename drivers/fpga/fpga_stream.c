#include "fpga_stream.h"
#include <string.h>
_Static_assert(__atomic_always_lock_free(4,0),"32-bit atomic publication must be lock free");
_Static_assert((UINT64_C(1)<<32)%FPGA_STREAM_STASH==0,"uint32 cursor wraps on storage boundary");
_Static_assert(FPGA_STREAM_STASH-FPGA_STREAM_LEASE>=2*FPGA_STREAM_BLOCK,"source reserve");
static uint32_t load(const uint32_t *p){return __atomic_load_n(p,__ATOMIC_ACQUIRE);}
static void store(uint32_t *p,uint32_t v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}
void fpga_stream_reset(fpga_stream_t *s,uint32_t origin)
{
    memset(s,0,sizeof *s);s->write=s->read=s->local=origin;
    s->absolute=origin;s->fence=origin;
    if(origin%FPGA_STREAM_BLOCK)store(&s->fatal,1);
}
void fpga_stream_poison(fpga_stream_t *s){if(s)store(&s->fatal,1);}
bool fpga_stream_publish(fpga_stream_t *s,uint32_t offset,const void *data,size_t n)
{
    if(!s||load(&s->fatal))return false;
    uint32_t w=load(&s->write),r=load(&s->read);
    if(!data||n!=FPGA_STREAM_BLOCK||offset!=w||w-r>FPGA_STREAM_STASH-FPGA_STREAM_BLOCK){fpga_stream_poison(s);return false;}
    memcpy(s->stash+w%FPGA_STREAM_STASH,data,n);store(&s->write,w+FPGA_STREAM_BLOCK);return true;
}
static bool advance(fpga_stream_t *s,uint32_t observed,uint32_t n,uint64_t *begin)
{
    if(observed!=s->local||n>FPGA_STREAM_STASH||n%FPGA_STREAM_BLOCK||s->absolute>UINT64_MAX-n)return false;
    *begin=s->absolute;s->absolute+=n;s->local+=n;return true;
}
link_stream_result fpga_stream_begin(fpga_stream_t *s,const uint8_t q[8])
{
    if(!s||s->active)return LINK_STREAM_INVALID;
    /* A rejected new epoch must never expose the previous successful body. */
    s->parser.closed=false;s->parser.result=LINK_STREAM_INVALID;
    if(!q||load(&s->fatal)||(q[1]&127)!=TRI_TILE_READ||!(q[1]&128))return LINK_STREAM_INVALID;
    uint32_t r=load(&s->read),boundary=load(&s->write);uint64_t discarded;
    if(!advance(s,r,boundary-r,&discarded)){fpga_stream_poison(s);return LINK_STREAM_GAP;}
    store(&s->read,boundary);s->epoch_begin=s->absolute;
    uint64_t fence=s->fence>s->absolute?s->fence:s->absolute;
    link_stream_result result=link_stream_begin(&s->parser,q,TRI_TILE_RESPONSE_BYTES,fence,FPGA_STREAM_WINDOW,s->window,sizeof s->window);
    s->active=result==LINK_STREAM_NEED_MORE;return result;
}
link_stream_result fpga_stream_poll(fpga_stream_t *s)
{
    if(!s||!s->active)return LINK_STREAM_INVALID;
    if(load(&s->fatal))return s->parser.result=LINK_STREAM_GAP;
    uint32_t r=load(&s->read),w=load(&s->write);uint64_t absolute;
    if(w-r<FPGA_STREAM_BLOCK)return s->parser.result;
    if(w-r>FPGA_STREAM_STASH||!advance(s,r,FPGA_STREAM_BLOCK,&absolute)){fpga_stream_poison(s);return s->parser.result=LINK_STREAM_GAP;}
    link_stream_result result=link_stream_feed(&s->parser,absolute,s->stash+r%FPGA_STREAM_STASH,FPGA_STREAM_BLOCK);
    store(&s->read,r+FPGA_STREAM_BLOCK);return result;
}
link_stream_result fpga_stream_finish(fpga_stream_t *s,bool proof)
{
    if(!s||!s->active)return LINK_STREAM_INVALID;
    s->active=false;
    if(!proof||load(&s->fatal)||load(&s->read)!=load(&s->write)){
        fpga_stream_poison(s);return s->parser.result=LINK_STREAM_GAP;
    }
    link_stream_result result=link_stream_finalize(&s->parser);
    if(result==LINK_STREAM_UNIQUE){
        if(s->parser.matched_end<s->epoch_begin||s->parser.matched_end-s->epoch_begin>FPGA_STREAM_LEASE){
            fpga_stream_poison(s);return s->parser.result=LINK_STREAM_OVERFLOW;
        }
        s->fence=s->parser.matched_end;
    }
    return result;
}
const uint8_t *fpga_stream_body(const fpga_stream_t *s)
{
    return s&&!s->active&&!load(&s->fatal)?link_stream_body(&s->parser):NULL;
}
