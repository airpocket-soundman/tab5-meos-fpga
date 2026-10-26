#pragma once
/* Bringup-v0 only: not the GPU command ABI or a DDR memory protocol. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define FPGA_LINK_FRAME_SIZE 8u
#define FPGA_LINK_TIMEOUT_MAX_US 1000000u
#define FPGA_LINK_POLL_LIMIT 1024u

enum { FPGA_LINK_ID=1, FPGA_LINK_ECHO=2, FPGA_LINK_INVERT=3 };
enum { FPGA_LINK_OK=0, FPGA_LINK_INVALID=-1, FPGA_LINK_IO=-2,
       FPGA_LINK_TIMEOUT=-3, FPGA_LINK_CANCELLED=-4, FPGA_LINK_CRC=-5,
       FPGA_LINK_SEQUENCE=-6, FPGA_LINK_REMOTE=-7, FPGA_LINK_DATA=-8,
       FPGA_LINK_BUSY=-9 };

typedef struct {
    /* Callbacks must be bounded by deadline. Positive means bytes transferred,
     * zero means no progress, negative means failure. No GPIO implementation yet. */
    int (*begin)(void *context, uint64_t deadline);
    int (*write)(void *context, const uint8_t *data, size_t length, uint64_t deadline);
    int (*turnaround)(void *context, uint64_t deadline);
    int (*read)(void *context, uint8_t *data, size_t length, uint64_t deadline);
    void (*finish)(void *context, bool failed);
    uint64_t (*now_us)(void *context);
    void (*idle)(void *context);
    bool (*cancelled)(void *context);
} fpga_link_ops_t;

typedef struct {
    const fpga_link_ops_t *ops;
    void *context;
    uint32_t attempted, completed, failed;
    uint8_t sequence;
    bool busy;
} fpga_link_t;

uint16_t fpga_link_crc16(const uint8_t *data, size_t length);
void fpga_link_request(uint8_t frame[8], uint8_t opcode, uint8_t sequence, uint8_t a, uint8_t b);
int fpga_link_response(const uint8_t response[8], const uint8_t request[8]);
/* Owner-serialized; not a cross-task lock or hardware bus owner. Reentry rejected.
 * finish is called once for every begun attempt, even when begin itself fails.
 * Failed responses never modify result. No heap allocations. */
int fpga_link_exchange(fpga_link_t *link, uint8_t opcode, uint8_t a, uint8_t b,
                       uint8_t result[2], uint32_t timeout_us);
const char *fpga_link_error(int result);
