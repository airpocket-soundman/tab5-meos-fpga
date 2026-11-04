#include "fpga_wire.h"
#include "../../third_party/carrier_link/link_sync.h"
#include "../../third_party/carrier_link/triangle_batch.h"
#include <string.h>

static size_t expected_body(uint8_t opcode)
{
    switch(opcode&0x7f) {
    case TRI_ROW:return FPGA_WIRE_ROW_BYTES;
    case TRI_TILE_READ:return FPGA_WIRE_TILE_BYTES;
    default:return 8;
    }
}
static bool request_valid(const uint8_t q[8])
{
    if(!q||q[0]!=0xa5||q[3]!=2)return false;
    uint16_t crc=link_crc16(q,6);
    return q[6]==(uint8_t)crc&&q[7]==(uint8_t)(crc>>8);
}
static int header_error(const uint8_t q[8],const uint8_t r[8])
{
    uint16_t crc=link_crc16(r,6);
    if(r[6]!=(uint8_t)crc||r[7]!=(uint8_t)(crc>>8))return FPGA_LINK_CRC;
    if(r[0]!=0x5a||r[3]!=2)return FPGA_LINK_INVALID;
    if(r[2]!=q[2])return FPGA_LINK_SEQUENCE;
    switch(r[1]) {
    case 0:return 0;
    case 2:return FPGA_LINK_UNSUPPORTED;
    case 3:return FPGA_LINK_RANGE;
    case 5:return FPGA_LINK_UNINITIALIZED;
    default:return FPGA_LINK_REMOTE;
    }
}
int fpga_wire_validate_body(const uint8_t q[8],const uint8_t *body,
                            size_t received,size_t expected)
{
    if(!request_valid(q)||!body||expected!=expected_body(q[1])||received!=expected)
        return FPGA_LINK_INVALID;
    int error=header_error(q,body);if(error)return error;
    bool valid=expected==FPGA_WIRE_TILE_BYTES?link_validate_tile_response(body,q):
               expected==FPGA_WIRE_ROW_BYTES?link_validate_row_response(body,q):
               link_validate_response(body,q);
    return valid?0:expected==8?FPGA_LINK_DATA:FPGA_LINK_CRC;
}
int fpga_wire_validate(const uint8_t q[8],const uint8_t *raw,size_t raw_length,
                       uint8_t *body,size_t body_length,unsigned *offset)
{
    if(!request_valid(q)||!raw||!body||body_length!=expected_body(q[1]))return FPGA_LINK_INVALID;
    unsigned at=0;
    if(q[1]&LINK_SYNC_TAG) {
        if(raw_length!=body_length+8&&raw_length!=body_length+16)return FPGA_LINK_INVALID;
        if(!link_sync_unwrap(raw,(unsigned)raw_length,q,body,(unsigned)body_length,&at))return FPGA_LINK_DATA;
    } else {
        /* Device errors are always ordinary8-byte bodies, including ROW/TILE. */
        if(raw_length==8) {
            int error=header_error(q,raw);
            if(error)return error;
        }
        if(raw_length!=body_length)return FPGA_LINK_INVALID;
        int error=header_error(q,raw);
        if(error)return error;
        bool valid=body_length==FPGA_WIRE_TILE_BYTES?link_validate_tile_response(raw,q):
                   body_length==FPGA_WIRE_ROW_BYTES?link_validate_row_response(raw,q):
                   link_validate_response(raw,q);
        if(!valid)return body_length==8?FPGA_LINK_DATA:FPGA_LINK_CRC;
        memmove(body,raw,body_length);
    }
    if(offset)*offset=at;
    return 0;
}
int fpga_wire_init(fpga_wire_t *w,const fpga_wire_ops_t *ops,void *context,
                   fpga_wire_workspace_t *workspace)
{
    if(!w||!ops||!ops->exchange||!ops->now_us||!workspace)return FPGA_LINK_INVALID;
    memset(&workspace->failure,0,sizeof workspace->failure);
    memset(w,0,sizeof *w);w->ops=ops;w->context=context;w->workspace=workspace;w->capture_margin=16;
    return 0;
}
int fpga_wire_margin(fpga_wire_t *w,unsigned bytes)
{
    if(!w||(bytes!=8&&bytes!=16))return FPGA_LINK_INVALID;
    if(w->busy)return FPGA_LINK_BUSY;
    w->capture_margin=(uint8_t)bytes;return 0;
}
int fpga_wire_transport(fpga_wire_t *w,const fpga_wire_ops_t *ops,void *context)
{
    if(!w||!ops||!ops->exchange||!ops->now_us)return FPGA_LINK_INVALID;
    if(w->busy)return FPGA_LINK_BUSY;
    w->ops=ops;w->context=context;return 0;
}
static int checkpoint(fpga_wire_t *w,uint64_t deadline)
{
    if(w->ops->cancelled&&w->ops->cancelled(w->context))return FPGA_LINK_CANCELLED;
    return w->ops->now_us(w->context)>=deadline?FPGA_LINK_TIMEOUT:0;
}
static bool overlaps(const void *a,size_t n,const void *b,size_t m)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(n>UINTPTR_MAX-x||m>UINTPTR_MAX-y)return true;
    return x<y+m&&y<x+n;
}
static int transact(fpga_wire_t *w,uint8_t op,uint8_t a,uint8_t b,
                    const uint16_t *records,unsigned count,bool tagged,
                    uint8_t *out,size_t length,uint32_t timeout_us)
{
    if(!w||!w->ops||!w->ops->exchange||!w->ops->now_us||!w->workspace||!out||
       !op||(op&0x80)||!timeout_us||timeout_us>FPGA_LINK_TIMEOUT_MAX_US||
       length!=expected_body(op)||(w->capture_margin!=8&&w->capture_margin!=16))return FPGA_LINK_INVALID;
    if(w->busy)return FPGA_LINK_BUSY;
    if(overlaps(out,length,w,sizeof *w)||overlaps(out,length,w->workspace,sizeof *w->workspace))return FPGA_LINK_INVALID;
    if(tagged&&!w->sync_capable)return FPGA_LINK_UNSUPPORTED;
    if(op==TRI_TILE_READ&&!w->tile_capable)return FPGA_LINK_UNSUPPORTED;
    if(op==TRI_BATCH&&(!w->batch_capable||!records||count<1||count>64))
        return !w->batch_capable?FPGA_LINK_UNSUPPORTED:FPGA_LINK_INVALID;
    if(records&&overlaps(records,count*20u,w->workspace,sizeof *w->workspace))return FPGA_LINK_INVALID;
    uint64_t now=w->ops->now_us(w->context);
    if(now>UINT64_MAX-timeout_us)return FPGA_LINK_INVALID;
    uint64_t deadline=now+timeout_us;
    w->busy=true;w->attempted++;
    uint8_t *tx=w->workspace->request,*rx=w->workspace->capture;
    unsigned tx_length=8;
    if(op==TRI_BATCH)tx_length=triangle_batch_encode(tx,FPGA_WIRE_REQUEST_MAX,w->sequence++,tagged,records,count);
    else link_make_request(tx,(uint8_t)(op|(tagged?LINK_SYNC_TAG:0)),w->sequence++,a,b);
    int error=checkpoint(w,deadline);
    size_t received=0,capacity=length+(tagged&&!w->ops->normalized_body?w->capture_margin:0);
    if(!error) {
        error=w->ops->exchange(w->context,tx,tx_length,rx,capacity,&received,deadline);
        if(error>0)error=FPGA_LINK_IO;
    }
    /* Stage the bounded original capture before in-place canonical unwrap.
     * Do not read past the actual reported bytes or allocated capture capacity. */
    fpga_wire_failure_t *failure=&w->workspace->failure;
    if(!failure->valid) {
        failure->request_length=tx_length;failure->received_length=received;
        failure->saved_length=received<capacity?received:capacity;
        if(failure->saved_length>FPGA_WIRE_FAILURE_BYTES)failure->saved_length=FPGA_WIRE_FAILURE_BYTES;
        failure->truncated=failure->saved_length<received;
        memcpy(failure->request_header,tx,8);
        memcpy(failure->raw,rx,failure->saved_length);
    }
    if(!error)error=checkpoint(w,deadline);
    if(!error&&received>capacity)error=FPGA_LINK_IO;
    /* Validate into the private capture workspace; only commit after the final
     * cancellation/deadline checkpoint. Callback cannot touch caller output. */
    if(!error)error=w->ops->normalized_body?fpga_wire_validate_body(tx,rx,received,length):
                    fpga_wire_validate(tx,rx,received,rx,length,NULL);
    if(!error)error=checkpoint(w,deadline);
    if(!error){memmove(out,rx,length);w->completed++;}else {
        w->failed++;
        if(!failure->valid){failure->error=error;failure->valid=true;}
    }
    w->busy=false;return error;
}
int fpga_wire_command(fpga_wire_t *w,uint8_t op,uint8_t a,uint8_t b,bool tagged,
                      uint8_t *body,size_t length,uint32_t timeout_us)
{
    if(op==TRI_BATCH)return FPGA_LINK_INVALID;
    return transact(w,op,a,b,NULL,0,tagged,body,length,timeout_us);
}
int fpga_wire_id(fpga_wire_t *w,uint8_t id[2],uint32_t timeout_us)
{
    if(!w||!w->workspace||!id||overlaps(id,2,w,sizeof *w)||
       overlaps(id,2,w->workspace,sizeof *w->workspace))return FPGA_LINK_INVALID;
    uint8_t body[8];int error=fpga_wire_command(w,LINK_ID,0,0,false,body,8,timeout_us);
    if(!error){id[0]=body[4];id[1]=body[5];}return error;
}
static int query(fpga_wire_t *w,uint8_t op,uint32_t timeout_us)
{
    if(!w)return FPGA_LINK_INVALID;
    if(w->busy)return FPGA_LINK_BUSY;
    bool *grant=op==LINK_CAPS?&w->sync_capable:op==TRI_TILE_CAPS?&w->tile_capable:&w->batch_capable;
    *grant=false;uint8_t body[8];
    int error=fpga_wire_command(w,op,0,0,false,body,8,timeout_us);
    if(!error)*grant=true;
    return error;
}
int fpga_wire_caps(fpga_wire_t *w,uint32_t t){return query(w,LINK_CAPS,t);}
int fpga_wire_tile_caps(fpga_wire_t *w,uint32_t t){return query(w,TRI_TILE_CAPS,t);}
int fpga_wire_batch_caps(fpga_wire_t *w,uint32_t t){return query(w,TRI_BATCH_CAPS,t);}
int fpga_wire_row(fpga_wire_t *w,unsigned row,unsigned plane,bool tag,uint8_t body[74],uint32_t t)
{
    if(row>=32||plane>1)return FPGA_LINK_RANGE;
    return fpga_wire_command(w,TRI_ROW,(uint8_t)row,(uint8_t)plane,tag,body,74,t);
}
int fpga_wire_tile(fpga_wire_t *w,unsigned plane,bool tag,uint8_t body[2058],uint32_t t)
{
    if(plane>1)return FPGA_LINK_RANGE;
    return fpga_wire_command(w,TRI_TILE_READ,0,(uint8_t)plane,tag,body,2058,t);
}
int fpga_wire_batch(fpga_wire_t *w,const uint16_t *records,unsigned count,bool tag,uint8_t body[8],uint32_t t)
{
    return transact(w,TRI_BATCH,(uint8_t)count,1,records,count,tag,body,8,t);
}

const fpga_wire_failure_t *fpga_wire_first_failure(const fpga_wire_t *w)
{
    return w&&w->workspace&&w->workspace->failure.valid?&w->workspace->failure:NULL;
}
