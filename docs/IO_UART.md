# Common UART API

`m5os/io_uart.h` is shared by native Shell and future MicroPython bindings. It uses the common IO owner and pin registry. The initial Tab5 backend provides one exclusive UART1 object, polling its hardware FIFOs without FreeRTOS, interrupts, DMA or dynamic allocation. Handles are monotonically allocated and never reused, including after close. Opening another object returns `ERR_BUSY`.

Configuration is controller 1, separate allowed TX/RX pins and baud 9600–1000000, always 8 data bits, no parity, one stop bit. No CTS/RTS or software flow control is implemented. Suggested external wiring is TX GPIO6, RX GPIO7 and common ground, at 3.3 V logic; connect TX to the peer's RX. These are logic UART pins, not an RS232/RS485 electrical interface. Opening configures TX to idle high. No probe data is transmitted automatically.

The pin registry rejects system pins, pins held by another peripheral and overlapping objects even from the same owner. UART1's default IOMUX pins GPIO10/11 are deliberately avoided because they are used by the C6 SDIO connection. GPIO20/21/34 belong to the board RS485 interface; this API does not configure that transceiver. UART0 and LP UART are unavailable. See the [official Tab5 pin map](https://docs.m5stack.com/en/core/Tab5).

## Operations

- `io_uart_open(owner, &config, &handle)` claims both pins atomically, configures UART1 using the GPIO matrix, and returns zero. Clock is the Tab5 40 MHz XTAL; baud uses the UART fractional divider. Register synchronization has a 2 ms deadline and an iteration bound. A failed setup releases both pins.
- `io_uart_read` and `io_uart_write` return a byte count or negative error. Length must not exceed 65536; timeout is 0–60000 ms. Zero timeout polls once, returns zero if empty/full and does not sleep. Positive timeout applies to the entire call, including partial progress; a read attempts to fill the requested length. A partial count takes precedence over a later timeout, cancellation or RX error. No-progress timeout returns `ERR_TIMEOUT`.
- Write counts bytes accepted by the FIFO. `io_uart_flush` waits for both FIFO and serializer to become idle; timeout zero returns `ERR_BUSY` if transmission is ongoing.
- Read and write may run concurrently; two reads or two writes/flushes on the same handle return `ERR_BUSY`. Direction reservations record the calling task ID; a later call recovers a reservation left by a terminated task. Calls are task-context only. Waiting yields through the native scheduler in 1 ms intervals; deadline precision is therefore scheduler dependent.
- `io_uart_close` cancels pending operations and discards queued data. Call flush before close when wire completion is required. Pins return to input/no pull. Owner destruction calls `io_uart_release_owner` outside the registry lock and performs the same cleanup, even while the owner is closing.
- Stale handles return `ERR_BADF`; a different owner cannot operate a valid handle (`ERR_PERM`). A pending operation interrupted by close or owner destruction returns `IO_ERR_CANCELLED` unless it already transferred bytes. Handle wrap exhausts allocation rather than reusing stale IDs.

RX FIFO overflow, parity and framing flags produce `ERR_IO`; there is no lossless buffering or flow control. Polling can lose input when callers are delayed, especially near 1 Mbaud. Use short bounded reads and a suitable peer data rate. No background receive task or automatic protocol framing is provided.

## Verification

`python tests/io/run_uart_test.py --cc <zig.exe>` compiles the real common driver with a deterministic FIFO/clock backend. Fixtures cover configuration rejection, pin rollback, controller exclusion, owner isolation, partial reads/writes, total deadlines, empty/full nonblocking calls, RX errors, flush, direction exclusion, owner-close cancellation, handle replacement during a wait, stale handle rejection, dead-task direction recovery and handle invalidation before pin release.

These fixtures do not verify electrical wiring, UART1 register behavior or physical baud accuracy. External TX/RX loopback and connected-device tests remain pending. Opening or writing UART is never part of boot or a test fixture on the device.


## Internal controller self-test

`io_uart_selftest()` returns zero for PASS or a negative error; Shell exposes this as `uart selftest`. An open UART1 handle causes `ERR_BUSY`. With the controller lock held, the test starts UART1 at 115200 8N1, enables the internal TX-to-RX loopback bit, compares eight known bytes and waits for idle. The transfer deadline is 20 ms with an independent iteration cap. Configuration synchronization is separately bounded to 2 ms per update. All exits stop/reset the controller, clear loopback/FIFOs and disconnect the peripheral RX input.

The self-test does not claim or configure GPIO pads, connect a TX output route, or change external output-enable registers. It does not use the normal `open` function. Thus it exercises internal serialization/reception without driving external pins. A second host fixture compiles the actual Tab5 register path against fake MMIO/LL declarations; it asserts zero GPIO pad/output-route operations, successful loopback, busy refusal, corruption detection, transfer timeout, synchronization failure and cleanup/retry. Actual silicon behavior remains a separate device test.

Device validation (2026-10-23): the integrated Tab5 build completed and the device's `uart selftest` returned `ok` using the internal loopback path. The surrounding IO/display regression reported 19/19 checks with zero errors, and allocation failures remained zero. This confirms internal UART1 initialization, serialization and reception on the tested firmware. External wired TX/RX, physical baud accuracy, high-rate sustained reception and connected peripheral operation remain unverified.
