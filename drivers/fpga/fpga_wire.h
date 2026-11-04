#pragma once
/* Portable carrier53c0239 host engine. No GPIO, DMA, scheduler or heap. */
#include <m5os/fpga_link.h>

#define FPGA_WIRE_REQUEST_MAX 1290u
#define FPGA_WIRE_BODY_MAX 2058u
#define FPGA_WIRE_CAPTURE_MAX 2074u
#define FPGA_WIRE_ROW_BYTES 74u
#define FPGA_WIRE_TILE_BYTES 2058u

typedef struct {
    /* One complete physical transaction, including turnaround and cleanup.
     * Return0 on completed capture, otherwise a negative FPGA_LINK_* error.
     * rx_count is actual observed bytes, never padded/reconstructed. The
     * callback owns buffers only until it returns and must honor deadline. */
    int (*exchange)(void *context,const uint8_t *tx,size_t tx_length,
                    uint8_t *rx,size_t rx_capacity,size_t *rx_count,uint64_t deadline);
    uint64_t (*now_us)(void *context);
    bool (*cancelled)(void *context);
    /* Explicit trusted framing boundary: exchange returns only actual body
     * bytes after its own UNIQUE closed-horizon stream validation. Wire still
     * validates exact size, request binding, semantics and full protocolCRC.
     * false retains the original finite tagged body+8/+16 capture contract. */
    bool normalized_body;
} fpga_wire_ops_t;

#define FPGA_WIRE_FAILURE_BYTES 96u
typedef struct {
    bool valid, truncated;
    int error;
    size_t request_length, received_length, saved_length;
    uint8_t request_header[8], raw[FPGA_WIRE_FAILURE_BYTES];
} fpga_wire_failure_t;

typedef struct {
    /* First attempted transaction failure, original bytes before unwrap.
     * Large captures retain only the first96 bytes; LINK24 is retained whole.
     * Never used to accept/correct/retry a response. Reset only by init. */
    fpga_wire_failure_t failure;
    uint8_t request[FPGA_WIRE_REQUEST_MAX];
    uint8_t capture[FPGA_WIRE_CAPTURE_MAX];
} fpga_wire_workspace_t;

typedef struct {
    const fpga_wire_ops_t *ops;
    void *context;
    fpga_wire_workspace_t *workspace;
    uint32_t attempted,completed,failed;
    uint8_t sequence,capture_margin;
    bool busy,sync_capable,tile_capable,batch_capable;
} fpga_wire_t;

int fpga_wire_init(fpga_wire_t *wire,const fpga_wire_ops_t *ops,void *context,
                   fpga_wire_workspace_t *workspace);
/* Owner-serialized; not a cross-task mutex. Caller retains workspace for the
 * entire session. Output buffers may not overlap wire or workspace. No replay.
 * Tagged use requires successful untagged LINK_CAPS; TILE/BATCH require their
 * capabilities too. Capability queries clear their old grant before probing. */
int fpga_wire_margin(fpga_wire_t *wire,unsigned bytes); /*8 or16(default)*/
/* Change physical strategy after untagged negotiation, preserving sequence and
 * negotiated capabilities. Must be called while idle, by the session owner. */
int fpga_wire_transport(fpga_wire_t *wire,const fpga_wire_ops_t *ops,void *context);
int fpga_wire_command(fpga_wire_t *wire,uint8_t opcode,uint8_t a,uint8_t b,
                      bool tagged,uint8_t *body,size_t body_length,uint32_t timeout_us);
int fpga_wire_id(fpga_wire_t *wire,uint8_t id[2],uint32_t timeout_us);
int fpga_wire_caps(fpga_wire_t *wire,uint32_t timeout_us);
int fpga_wire_tile_caps(fpga_wire_t *wire,uint32_t timeout_us);
int fpga_wire_batch_caps(fpga_wire_t *wire,uint32_t timeout_us);
int fpga_wire_row(fpga_wire_t *wire,unsigned row,unsigned plane,bool tagged,
                  uint8_t body[FPGA_WIRE_ROW_BYTES],uint32_t timeout_us);
int fpga_wire_tile(fpga_wire_t *wire,unsigned plane,bool tagged,
                   uint8_t body[FPGA_WIRE_TILE_BYTES],uint32_t timeout_us);
int fpga_wire_batch(fpga_wire_t *wire,const uint16_t *records,unsigned count,
                    bool tagged,uint8_t body[8],uint32_t timeout_us);
/* Standalone strict validator. Untagged raw is the exact body; tagged raw is
 * body+8 or body+16 actual bytes. Exactly one canonical valid body is required.
 * Output and offset remain untouched on failure; raw/body may overlap. */
int fpga_wire_validate(const uint8_t request[8],const uint8_t *raw,size_t raw_length,
                       uint8_t *body,size_t body_length,unsigned *offset);

/* Returns NULL until an attempted transaction fails. Storage belongs to workspace. */
const fpga_wire_failure_t *fpga_wire_first_failure(const fpga_wire_t *wire);

/* Exact actualbody only; no sync search, insertion, repair or prefix synthesis. */
int fpga_wire_validate_body(const uint8_t request[8],const uint8_t *body,
                            size_t received_length,size_t expected_length);
