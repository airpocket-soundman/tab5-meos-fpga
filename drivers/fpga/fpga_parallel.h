#pragma once
#include "fpga_gpio.h"
#include "fpga_wire.h"
#include "fpga_stream.h"
#include <m5os/hal_parlio.h>
typedef struct {
  io_owner_t owner;
  hal_parlio_t *native;
  bool claimed, failed, busy, native_rx, tagged, owner_guarded;
  /* Explicit owner-selected GPIO control timing: 1us setup/half, 2us guards.
   * Select only while idle. Default preserves 100us/200us diagnostics. */
  bool fast_control;
  uint8_t short_failure[24];
  size_t short_failure_length;
  uint32_t gpio_tx_completed, max_gap_us;
  uint64_t last_check_us;
  int error;
  uint32_t attempted, completed;
  uint8_t pending[8];
  size_t pending_length;
  bool (*cancel)(void *);
  void *cancel_context;
  hal_parlio_stats_t stats;
  fpga_stream_t *stream;
  hal_parlio_stream_status_t stream_stats;
} fpga_parallel_t;
/* Zero initialize; caller serializes the whole command, not just exchanges. */
int fpga_parallel_open(fpga_parallel_t *, const char *profile, uint32_t hz);
void fpga_parallel_close(fpga_parallel_t *);
int fpga_parallel_continuous(fpga_parallel_t *);
extern const fpga_wire_ops_t fpga_parallel_wire_ops;
extern const fpga_wire_ops_t fpga_parallel_stream_wire_ops;
extern const fpga_link_ops_t fpga_parallel_link_ops;
