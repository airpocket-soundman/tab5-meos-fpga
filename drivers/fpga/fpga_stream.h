#pragma once
/* C adaptation of tab5-fpga-gpu7a4f83e main/dma_stream_queue.hpp/profile.hpp.
 * Scaled profile only. Caller allocates this ~16KiB object off task stack.
 * One producer / one consumer; reset requires both stopped. No DMA pointers
 * retained. GCC lock-free32-bit acquire/release atomics match source queue. */
#include "../../third_party/carrier_link/link_stream.h"
#define FPGA_STREAM_BLOCK 1024u
#define FPGA_STREAM_RING 4096u
#define FPGA_STREAM_STASH 8192u
#define FPGA_STREAM_WINDOW 8192u
#define FPGA_STREAM_LEASE 5120u
typedef struct {
    uint32_t write,read,fatal;
    uint32_t local;
    uint64_t absolute,fence,epoch_begin;
    bool active;
    link_stream parser;
    uint8_t stash[FPGA_STREAM_STASH],window[FPGA_STREAM_WINDOW];
} fpga_stream_t;
void fpga_stream_reset(fpga_stream_t *,uint32_t origin);
/* Producer: offset is monotonic observed uint32 byte coordinate (wrap allowed); only whole actual blocks.
 * Bad order/length or overflow permanently poison session until reset. */
bool fpga_stream_publish(fpga_stream_t *,uint32_t byte_offset,const void *,size_t);
void fpga_stream_poison(fpga_stream_t *);
/* Consumer: call with sampling OFF and CLK unmapped; discards only bytes
 * already published, retaining unfinished hardware descriptor/carry. TILE only. */
link_stream_result fpga_stream_begin(fpga_stream_t *,const uint8_t request[8]);
/* Drain at most one published block; actual absolute byte offsets only. */
link_stream_result fpga_stream_poll(fpga_stream_t *);
/* Caller must prove ISR capture closed, READY low, pending callbacks drained,
 * noFIFO/cache/DMA/phase errors, and validation/CLK closed before final commit.
 * false is fatal, never provisional success. Output only after UNIQUE. */
link_stream_result fpga_stream_finish(fpga_stream_t *,bool closure_proven);
const uint8_t *fpga_stream_body(const fpga_stream_t *);
