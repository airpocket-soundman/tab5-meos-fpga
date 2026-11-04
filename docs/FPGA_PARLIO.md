# Native PARLIO / AXI GDMA host transport

## Reference and ownership

Reviewed carrier origin/main: `53c023929b4f8cf42a6e02725160c0f0015dd162`.
Reviewed ESP-IDF host origin/main: `7a4f83e85978ce2c9a94dc3df0322636d7a7b355`.
The carrier repository owns the FPGA RTL, constraints, bitstreams and command
specification. This repository implements the MeOS host. See
[carrier requirements](FPGA_CARRIER_REQUIREMENTS.md) for electrical review and
[LINK](FPGA_LINK.md) for the separate low-speed GPIO backend.

`fpga_parlio` is optional and depends on `fpga`. The electrical-review build
gate remains required. No pin activation or automatic test runs at boot.
The common IO service atomically reserves all15 Dock-profile pins before use.
An inactive profile does not permanently reserve those pins.

```text
fpga --parlio dock-jumper-v0 1000000 id
fpga --parlio dock-jumper-v0 1000000 test
```

The CLI also exposes capability, GPU and explicit hybrid diagnostics. Consult
its help for the current command list; `--gpio-rx` explicitly selects the
hybrid path and `--rx-hz HZ` selects an independent finite-DMA RX divider.

## Transport and API

The `hal_parlio` adapter uses polling and bounded deadlines rather than
FreeRTOS queues, callbacks or public ESP-IDF driver allocation. It reserves the
PARLIO module and unused AXI GDMA channels, preserving unrelated channels and
avoiding shared DMA/ETM resets. The LCD uses a separate DW-GDMA controller.
Native register reservations are not registered in ESP-IDF software allocators;
future AXI GDMA/ETM users must share the native reservation policy.

Internal DMA allocation includes128-byte-aligned, isolated descriptor and
payload storage with explicit cache synchronization. One request is outstanding
at a time. Completion requires actual EOF, descriptor ownership/count, cache,
phase and FIFO/error checks. Unknown completion is not automatically replayed.
Unproven DMA quiescence quarantines the allocation and peripheral ownership.
`hal_parlio_close` reports whether resources were actually released; callers
must retain the pin lease on failure.

The caller serializes open, transfers, rate changes and close. `tx` returns
zero only after success. `rx` initializes `received` to zero and publishes the
requested byte count only after complete validation; it never manufactures
missing bytes. Cancel/deadline failures do not publish partially captured data.
The optional hybrid path explicitly uses TX DMA with finite GPIO RX; it is
never reported as RX DMA.

TX uses16 internal lanes with `[byte, 0]` packing, routes only the eight payload
lanes and uses generated VALID on lane15. Keep the per-transfer TX core/FIFO
reset. Physical CLK stays GPIO LOW while resetting and stabilizing the clock.
IDF5.4.4 sample-edge enum programming differs from the5.5.2 reference. The
validated literal TX phase is `i_inv=1/o_inv=1`, matrix inversion0; RX is
`i_inv=0/o_inv=0`, matrix inversion1. Both paths verify programmed phase.

## Finite RX and internal VALID gate

RX bit length generates EOF but does not automatically stop sampling. A
polling task can resume too late and observe FIFO overflow even after a complete
descriptor. Therefore GPIO17, already owned as Dock AUX0 and unwired in the
tested11-signal arrangement, temporarily drives an internal VALID signal.
PARLIO RXline15 receives this signal outside the8-bit payload. The owned AXI
RX EOF event routes through an owned ETM channel to a GPIO CLR task, closing
VALID without waiting for CPU scheduling. This changes neither FPGA protocol
nor external wiring; GPIO17 must not be externally driven concurrently.

The IO service configures17 output LOW immediately before RX. The HAL makes
CPU GPIO output-enable authoritative, checks route/task/readback, and starts
sampling by setting17 HIGH only after DMA, receiver and clock are ready.
Completion proves17 LOW plus the latched EOF event and GPIO task before CPU
cleanup. ETM route IDs are1-based; status bits use `id-1`. Cleanup disables the
ETM route/task before restoring17 to input, including quarantine paths, so a
late event cannot modify a later pin owner. IRQ masking is not the stop mechanism.

## Clock selection and limits

TX supports500kHz,1,2 and5MHz. RX defaults to the TX rate. The software-only,
idle-only `hal_parlio_set_rx_frequency` setter accepts500kHz,1,2,5,8,10,16 and
20MHz. It does not change pins or clock registers until the next RX.500kHz
uses XTAL40MHz/div80; the other rates use exact integer PLL160MHz divisors.
Accepted divider settings do not imply hardware validation. No4MHz setting is
currently exposed. Continuous/ring DMA,40MHz operation and sustained bandwidth
remain separate work.

## Canonical protocol and GPU checks

The host follows variable request/response lengths, LINK_CAPS04, tagged opcode
bit7 with recomputed request CRC, `c3`×8 prefetch, and complete-body validation.
ROW responses are74 bytes and TILE responses2058 bytes; batch requests are at
most1290 bytes. Pixel data must come from actual FPGA readback and pass the
specified count/content/CRC checks. A successful identity exchange alone does
not prove GPU capability or DDR3 access.

The E368 filtered-batch image implements the TILE/batch engine; the older
`GFX_ENABLE1` command engine is incompatible. `gpu-test` and `gpu-verify` exercise
that legacy engine and are not expected to pass on E368. Use capability-aware
TILE/batch tests with the matching image. The recorded legacy verification
attempt in [this evidence](validation/parlio-gfx-verify-5mhz-2026-11-04.json)
reports a data-class result: the canonical synchronous unwrap currently rejects
a nonzero device status before classifying that status separately. This is a
known error-classification limitation, not evidence of DMA failure; validation
has not been weakened to make an incompatible command pass.

On2026-11-04, finite native TX/RX passed at1MHz and5MHz on the controlled Dock
wiring with the E368 filtered-batch SRAM image. At each rate: identity,
513 LINK transactions, six tile scenes/12288 words and three64-record batches/
6144 words passed. ETM gate event/task/LOW proof passed without FIFO overflow.
The frozen firmware SHA256 was
`8efab4aa17e84c5892baf440d6a677e8358348bd8e1e00245a0bac6f7f5722f8`.

| Rate | Six tile scenes | Three64-record batches | Evidence |
|---|---:|---:|---|
|1MHz|244746µs|83428µs|[record](validation/parlio-etm-indexfix-1mhz-2026-11-04.json)|
|5MHz|194280µs|30052µs|[record](validation/parlio-etm-5mhz-2026-11-04.json)|

These are bounded suites, not long-duration throughput or full-frame GUI
benchmarks. The separate hybrid path also passed six scenes/12288 words and
presented FPGA readback on the LCD. Peripheral-load stress, power-off isolation,
DDR3 allocation and GUI integration remain unverified.
The subsequent minimal-module split-frequency firmware (1178128 bytes,
SHA256 `290d245feb56233a27da3e1e422edd416fcd7bdef59d5d5dbc6da4d36562ba73`)
passed identity,513 transactions, six tile scenes and three batches at both
TX5MHz/RX5MHz and TX5MHz/RX8MHz. At TX5MHz/RX10MHz, identity passed but the
first echo failed data validation. Testing stopped;16MHz and20MHz were not
attempted. See [split-rate evidence](validation/parlio-split-rates-2026-11-04.json).
The10MHz failure is an experimental limit, not a supported operating point;
accepted divider settings remain distinct from tested operation. These minimal
module tests do not establish USB/Wi-Fi/peripheral-load coexistence.
