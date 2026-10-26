#pragma once
#include <m5os/fpga_link.h>
/* Software endpoint; no bus timing, DDR3 or GPIO ownership is modelled. */
enum {FPGA_MOCK_NORMAL,FPGA_MOCK_STALL_WRITE,FPGA_MOCK_STALL_READ,FPGA_MOCK_BAD_CRC,
      FPGA_MOCK_OLD_SEQUENCE,FPGA_MOCK_WRONG_DATA,FPGA_MOCK_TRUNCATE,FPGA_MOCK_IO,
      FPGA_MOCK_OVERSIZE,FPGA_MOCK_BEGIN_FAIL,FPGA_MOCK_TURN_FAIL};
typedef struct {
    uint8_t request[8],response[8];
    unsigned written,read,chunk,fault,finishes;
    uint64_t now;
    bool ready,cancel,frozen_clock,last_failed;
} fpga_mock_t;
void fpga_mock_init(fpga_mock_t *mock,unsigned chunk);
void fpga_mock_respond(const uint8_t request[8],uint8_t response[8]);
extern const fpga_link_ops_t fpga_mock_ops;
