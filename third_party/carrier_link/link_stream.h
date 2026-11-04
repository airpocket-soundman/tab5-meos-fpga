#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "link_protocol.h"

// Existing wire format only. CANDIDATE is provisional: acceptance requires
// explicit finalize() after the caller's documented bounded capture horizon.
typedef enum {
    LINK_STREAM_NEED_MORE=0, LINK_STREAM_CANDIDATE=1, LINK_STREAM_UNIQUE=2,
    LINK_STREAM_NO_MATCH=3, LINK_STREAM_AMBIGUOUS=4, LINK_STREAM_OVERFLOW=5,
    LINK_STREAM_GAP=6, LINK_STREAM_INVALID=7
} link_stream_result;

typedef struct {
    uint8_t request[8];
    uint8_t *window;
    size_t capacity,max_window_bytes,window_size,body_len,next_scan,matched_index;
    uint64_t min_fence,next_offset,window_base,matched_offset,matched_end;
    unsigned candidate_count;
    bool started,closed;
    link_stream_result result;
} link_stream;

static inline bool link_stream_failed(link_stream_result r) {
    return r==LINK_STREAM_AMBIGUOUS || r==LINK_STREAM_OVERFLOW ||
           r==LINK_STREAM_GAP || r==LINK_STREAM_INVALID;
}
static inline link_stream_result link_stream_begin(link_stream *s,const uint8_t q[8],
        size_t body_len,uint64_t min_fence,size_t max_window_bytes,
        uint8_t *window,size_t capacity) {
    if(!s) return LINK_STREAM_INVALID;
    memset(s,0,sizeof(*s));s->result=LINK_STREAM_INVALID;
    if(!q || !window || max_window_bytes>capacity || max_window_bytes<body_len)
        return s->result;
    const unsigned op=q[1]&0x7f;
    const size_t expected=op==TRI_TILE_READ?TRI_TILE_RESPONSE_BYTES:
                          (op==TRI_ROW?TRI_ROW_RESPONSE_BYTES:8);
    if(body_len!=expected || q[0]!=0xa5 || q[3]!=2) return s->result;
    const uint16_t crc=link_crc16(q,6);
    if(q[6]!=(uint8_t)crc || q[7]!=(uint8_t)(crc>>8)) return s->result;
    memcpy(s->request,q,8);s->window=window;s->capacity=capacity;
    s->max_window_bytes=max_window_bytes;s->body_len=body_len;s->min_fence=min_fence;
    return s->result=LINK_STREAM_NEED_MORE;
}

// Every chunk must contain actual observed bytes at its absolute stream offset.
// The first offset may precede the fence; only bytes before the fence are
// discarded. All later chunks must be contiguous, including discarded bytes.
// No padding, prefix, header, missing byte or protocol CRC is synthesized.
static inline link_stream_result link_stream_feed(link_stream *s,uint64_t offset,
        const uint8_t *data,size_t length) {
    if(!s) return LINK_STREAM_INVALID;
    if(link_stream_failed(s->result)) return s->result;
    if(s->closed || (!data && length)) return s->result=LINK_STREAM_INVALID;
    if(length>UINT64_MAX-offset) return s->result=LINK_STREAM_OVERFLOW;
    if(s->started && offset!=s->next_offset) return s->result=LINK_STREAM_GAP;
    if(!length) return s->result;
    s->started=true;s->next_offset=offset+length;
    size_t skip=0;
    if(offset<s->min_fence) {
        const uint64_t before=s->min_fence-offset;
        skip=before>length?length:(size_t)before;
    }
    const size_t kept=length-skip;
    if(kept>s->max_window_bytes-s->window_size)
        return s->result=LINK_STREAM_OVERFLOW;
    if(!kept) return s->result;
    if(!s->window_size) s->window_base=offset+skip;
    // memmove also permits callers to feed a currently disjoint part of the
    // same allocation. They must not overwrite previously retained bytes.
    memmove(s->window+s->window_size,data+skip,kept);s->window_size+=kept;
    while(s->window_size>=s->body_len && s->next_scan<=s->window_size-s->body_len) {
        const size_t i=s->next_scan++;
        const uint8_t *r=s->window+i;
        if(r[0]!=0x5a || r[1]!=0 || r[2]!=s->request[2] || r[3]!=2) continue;
        const unsigned op=s->request[1]&0x7f;
        const bool valid=op==TRI_TILE_READ?link_validate_tile_response(r,s->request):
                         (op==TRI_ROW?link_validate_row_response(r,s->request):
                          link_validate_response(r,s->request));
        if(valid) {
            if(++s->candidate_count>1) return s->result=LINK_STREAM_AMBIGUOUS;
            s->matched_index=i;s->matched_offset=s->window_base+i;
            s->matched_end=s->matched_offset+s->body_len;
        }
    }
    return s->result=s->candidate_count?LINK_STREAM_CANDIDATE:LINK_STREAM_NEED_MORE;
}

// Closes only the observed bounded window, not an unbounded future stream.
// A provisional candidate must never be rendered before this returns UNIQUE.
static inline link_stream_result link_stream_finalize(link_stream *s) {
    if(!s) return LINK_STREAM_INVALID;
    if(s->closed || link_stream_failed(s->result)) return s->result;
    s->closed=true;
    return s->result=s->candidate_count==1?LINK_STREAM_UNIQUE:LINK_STREAM_NO_MATCH;
}
static inline const uint8_t *link_stream_body(const link_stream *s) {
    return s && s->closed && s->result==LINK_STREAM_UNIQUE ? s->window+s->matched_index:NULL;
}
