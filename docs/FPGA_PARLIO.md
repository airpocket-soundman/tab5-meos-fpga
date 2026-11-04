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
hybrid path; `--finite-rx` selects the previous finite comparison and
`--rx-hz HZ` selects an independent receive divider.

## Transport and API

The finite adapter polls with bounded deadlines. Continuous RX uses an owned
DMA interrupt through the existing MeOS/IDF vector dispatcher and a copied-block
SPSC queue, without FreeRTOS tasks or queues. It reserves the
PARLIO module and unused AXI GDMA channels, preserving unrelated channels and
avoiding shared DMA/ETM resets. The LCD uses a separate DW-GDMA controller.
Native register reservations are not registered in ESP-IDF software allocators;
future AXI GDMA/ETM users must share the native reservation policy.

Internal DMA allocation includes128-byte-aligned, isolated descriptor and
payload storage with explicit cache synchronization. One request is outstanding
at a time. Finite completion checks actual EOF and descriptor ownership/count;
continuous completion checks the copied-block lease/closed parsing horizon, cache,
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

The finite comparison backend supports TX500kHz,1,2 and5MHz. RX defaults to the TX rate. The software-only,
idle-only `hal_parlio_set_rx_frequency` setter accepts500kHz,1,2,5,8,10,16 and
20MHz. It does not change pins or clock registers until the next RX.500kHz
uses XTAL40MHz/div80; the other rates use exact integer PLL160MHz divisors.
Accepted divider settings do not imply hardware validation. No4MHz setting is
currently exposed. Continuous mode accepts1MHz and above from the same set;
40MHz and sustained bandwidth remain outside this implementation.

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

**Controlled retest:** external reflashing was confirmed when FPGA UserCode9678
was found. Earlier failures do not establish continuous known-image conditions.
Both known images were restored: MeOS1178608 bytes, SHA256
`7d3221d711e09f2f535210db46cf69dd00b5b7dfedaf6cd1af58f2b6b65be7a8`,
and qualified E368 SRAM. Final1MHz/5MHz ID/513/tile6/batch3 suites passed,
with E368 checked before/after each rate. RX8MHz then failed after139 successes:
the24-byte capture contains a header at offset7 but its8-byte candidate CRC is
invalid. EOF/ETM proof passed and no FIFO error was reported.8MHz remains
experimental even with the controlled baseline. The final run stopped there;
10/16/20MHz were not attempted. MeOS flash subsequently matched the frozen
binary by digest. See [controlled evidence](validation/parlio-controlled-retest-2026-11-04.json).
No further timing/phase changes were attempted after the stop request.

## Production surface API: host-tested scope

`fpga_surface` adds a reusable32×32 RGB565 tile API over an existing serialized
wire session: begin/clear, queue up to64 triangles, explicit batch flush,
complete validated tile readback, and cached LCD presentation. Its caller-owned
workspace is5386 bytes (records1280 + wire body2058 + color cache2048), separate
from session/transport storage. Only a fully validated readback publishes an
image generation. A command failure poisons the surface until session reset;
there is no CPU pixel substitution, automatic replay or fallback. Presentation
uses a display batch and does not acquire a new physical session. This API has
host-test coverage only; it is not a verified GUI, DDR3 framebuffer or additional
hardware surface result.

## Full-feature integration build

Use `-DM5OS_PROFILE=large_fpga` to retain the complete `large` selection
(built-in camera, UVC/MJPEG, MicroPython/camera and SL) while adding
`fpga_parlio` and its `fpga` dependency. The existing three published firmware
profiles remain unchanged; this is an opt-in integration build. Physical
command authorization remains a separate build gate.

`tests/test_fpga_profile.py` exercises the real CMake resolver and native source
filter, including the production surface source. The optional FPGA CI workflow
builds `large` and `large_fpga` using ESP-IDF5.5.2, one identical sdkconfig and
compiler, then retains binary sizes/hashes and ELF section reports. A measured
resource delta is valid only after both matching builds succeed; no CI result
or full-feature resource comparison is claimed yet.

Local compile probes against the actual ESP-IDF5.5.2 headers (revision
`30aaf64524299d3bde422ca9a2848090d1bc5d0f`) passed the native HAL and built-in
camera source with the existing RISC-V compiler and Newlib selection. These are
syntax probes using the existing5.4.4-generated configuration plus the5.5.2
`CONFIG_LIBC_NEWLIB` selection, not full5.5.2 configure/link or hardware tests.
The local5.4.4 full-camera build has a pre-existing camera API mismatch; the
published firmware workflow already targets5.5.2. Minimal FPGA image sizes must
not be compared with camera/Python-enabled builds as the isolated FPGA cost.


## Continuous reference alignment (current default)

The default CLI receive policy is now `bulk-continuous+short-gpio`, following
GPU host7a4f83e's bulk-only division. Explicit `--finite-rx` retains the prior
ETM-gated finite comparison; `--gpio-rx` selects GPIO reception. Do not interpret
a successful short ID/echo test as a DMA bulk-rate qualification.

The native port matches the scaled1024x4 profile: four1024-byte DMA blocks,
8192-byte SPSC stash and parser window,5120-byte capture lease, internal
PLL160MHz clock with NEG register pair0/0 and inverted pad clock, soft
validation, persistent ring, READY-low plus one real trailing block closure,
and normal4096-byte ring EOF that does not destroy the session. The task drains
pending ISR publication before final UNIQUE/CRC acceptance. No prefix, missing
byte or pixel is manufactured. A normalized-body transport explicitly returns
only the actual validated2058-byte TILE body; finite tagged-window validation
is unchanged. DMA queue storage is internal, never a retained DMA-buffer view.

Initial cold attachment now happens while the Core clock is OFF; warm requests
enable soft validation before mapping the running clock. The bulk handoff waits
READY=1 with DIR=0, then CLK HIGH,2us,DIR HIGH,2us. DATA8/CLK/DIR use the source's
CAP0 drive with readback; saved drive settings return only after safe teardown.
GPIO17 is not used for VALID in continuous mode. Task termination/quarantine
retains callback memory and IO leases until safe teardown or reboot.

This is a native MeOS port, not the unmodified FreeRTOS driver binary. Allocation,
IRQ dispatch and waits use MeOS integration. Short GPIO replies still use
conservative100us software bit intervals, so source25ns GPIO timings and its
full-frame benchmark performance are not reproduced. Register-policy parity
alone cannot establish identical electrical timing.

Final frozen firmware SHA256
`6c41bd04b8d4196d4b054190d5a0be8fc31458cc0428ec34a6d3b46ea9d99425`
(1189568 bytes, minimal FPGA modules plus native-M5EX opt-in) passed tile6 scenes/
12288 color-depth words and3x64-record batches/6144 words at1MHz and5MHz.
RX8MHz failed strict CRC acceptance on the first tile; the test stopped before
10/16/20MHz. E368 was checked before/after each rate. Original raw failure windows
are retained in [final ready/drive parity test](validation/parlio-continuous-ready-parity-2026-11-04.json).
Earlier candidates passed8MHz once but do not override this final result. No
20MHz MeOS success, stable high-rate qualification or speedup is claimed.
The source handoff's20MHz success remains a separate recorded ESP-IDF result;
its efb525cf frozen binary was not found locally for an exact baseline rerun.

The standalone [fractal application](FRACTAL_APP.md) uses a conservative1MHz
GPU device call. Its successful1024-pixel comparisons validate that application,
not high-rate bulk operation, full GUI integration or DDR3.


## GPU fixed-policy transport update — 2026-11-04

The current `/dev/fpga` policy supersedes the conservative1MHz per-tile policy
above: short8-byte requests/replies use owned GPIO with1us setup/half-cycle
and2us guards; full TILE replies use continuous5MHz DMA. Native DMA TX remains
for long BATCH frames, configured at1MHz in the device policy. Short GPIO TX
is reported separately as `gpio_tx_completed`, not as DMA completion. The
CLI enables this division explicitly with `--fast-control`; its default
conservative diagnostic path is preserved.

`FPGA_IOC_FRACTAL_BEGIN` / `END` (NULL arguments) negotiate and release an
optional task-owned frame session. RENDER stays ABI-version1. Output is valid
only after complete validated readback; callers require successful END before
presenting a held frame. Communication failure closes without retry. A local
invalid argument leaves an existing session for explicit END/close. Other
tasks cannot use/end an owner's session. Owner close/death tears it down;
unsafe DMA teardown retains the kernel context and protected IO lease.

The synchronous GPU ioctl raises task priority only while servicing the
bounded operation and restores it on every return. DMA/LCD/USB interrupts
stay enabled; only short GPIO bit loops mask IRQs (nominal24us TX /72us RX,
not the whole frame). This addresses a measured148449us in-transaction gap
against the carrier100ms no-strobe limit. Original failures are preserved.

[Final qualification](validation/fpga-speed-summary-2026-11-04.json):1,152
tiles /1,179,648 exact pixels /72 sessions, with no transport failures, using
frozen E368. Fixed16-tile Mandelbrot time was1.86s before and97ms after; final
full-case time includes negotiation/close and CPU checking. Prior8MHz failure
and the absence of a20MHz MeOS qualification remain unchanged. No FPGA RTL,
clock phase, electrical allocation or power changes.
