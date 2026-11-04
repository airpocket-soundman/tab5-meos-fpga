#pragma once
#include "fpga_gpio.h"
#include "fpga_wire.h"
#include <m5os/hal_parlio.h>
typedef struct {
  io_owner_t owner;
  hal_parlio_t *native;
  bool claimed, failed, busy, native_rx, tagged, owner_guarded;
  int error;
  uint32_t attempted, completed;
  uint8_t pending[8];
  size_t pending_length;
  bool (*cancel)(void *);
  void *cancel_context;
  hal_parlio_stats_t stats;
} fpga_parallel_t;
/* Zero initialize; caller serializes the whole command, not just exchanges. */
int fpga_parallel_open(fpga_parallel_t *, const char *profile, uint32_t hz);
void fpga_parallel_close(fpga_parallel_t *);
extern const fpga_wire_ops_t fpga_parallel_wire_ops;
extern const fpga_link_ops_t fpga_parallel_link_ops;
