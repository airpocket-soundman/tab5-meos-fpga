# FPGA bringup-v0 protocol groundwork

`fpga status` reports the backend and profile gate. `fpga --mock id`, `fpga --mock echo 0x55 0xaa`, `fpga --mock invert 0x55 0xaa` and `fpga --mock selftest` explicitly select a software endpoint. No GPIO, PARLIO, DMA, DDR3 or electrical timing is exercised.

The independent codec uses 8-byte frames and CRC16/CCITT-FALSE. ID, echo and inversion responses validate framing, sequence, status and data. Transport callbacks support fragmented progress, deadlines, cancellation and cleanup. A bounded poll count also terminates a stalled clock. Clients require owner serialization; the reentry guard is not a cross-task lock. Backends must honor the deadline and buffer contract, including bounded finish/cleanup.

583 golden vectors from GPU revision 3b1ace3d91c6305ec0e32378f8819fce208c2aa7 matched response bytes and acceptance. Fixture provenance/hashes are in tests/fixtures/fpga_bringup_v0_source.json. Eight fragment sizes passed 4104 pattern exchanges and 96 fault checks; additional tests cover corruption, late completion, cancellation, sequence wrap, recursive entry and CLI rejection. Native Tab5 mock selftest passed 513 pattern exchanges and 12 fault checks, including ten additional successful recovery exchanges. Heap usage was unchanged. This validates software execution on Tab5, not FPGA connectivity.

Physical implementation requires the latest carrier pin map, IO ownership, direction/turnaround timing, safe power sequencing and timeout recovery to be verified first. Evidence: [host](validation/fpga-link-host-result.json), [Tab5 mock](validation/fpga-link-hardware-result.json). Run `python tests/test_fpga_link.py --cc PATH_TO_COMPILER`.

## Dock GPIO implementation (2026-11-03)

The Tab5 native build includes a one-shot bitbang backend. **It is built with `M5OS_FPGA_DOCK_VERIFIED=0`; physical commands reject before any IO access.** The current device firmware has not been replaced. Carrier specification review: origin/main c3502174c039bf86d8d43716e3ce0fd2fb4d8c43, local checkout e5a386d263d8911a490922cc2770292699a87080 plus uncommitted Dock top/CST/report. Those working files are not an immutable release; preserve hashes before real testing.

Prepared commands after electrical review and a separately enabled build:

- `fpga --gpio --arm dock-v0 id`: one ID exchange.
- `fpga --gpio --arm dock-v0 test`: ID then 256 echo/inversion pairs (513 exchanges), stopping on first failure.
- `fpga --mock selftest`: unchanged software-only fault suite.

Each physical command requires explicit ARM syntax. It does not toggle the FPGA's physical SW0. Each exchange atomically claims all eleven pins through the common IO service, starts with inputs, preloads CLK/DIR low, waits 110ms for the FPGA's 100ms idle watchdog, clocks with >=100us half-periods and releases data before waiting >=200us and switching DIR. Response is sampled before each read pulse. Every completed/failed attempt and CLI exit closes the owner and returns all pins to inputs. A new command does not preserve armed state. No boot GPIO configuration is introduced, so boot ROM/JTAG/other-driver pad states still need physical measurement. GPIO2..5 GPIO mux selection uses the existing HAL, not a permanent eFuse/JTAG setting.

Ctrl-C/Escape from the serial console cancels at checkpoints; other typeahead during the transfer is consumed. SSH cancellation and forced task-kill during the output interval have not been validated; use the serial shell for the first trial. IO dead-owner reaping is deferred and cannot guarantee immediate release after a forced kill. Callbacks return bounded errors; low-level timeout/cancel may appear as `io` in the protocol result. Aggregate counts identify how far a real test progressed, but no physical-link success is persisted or claimed by `status`.

Host regression `python tests/test_fpga_gpio.py --cc PATH` runs the actual backend with profile gate both disabled and test-enabled, using an event-level GPIO/endpoint model. It checks zero IO activity when unverified, 513 exchanges, owner conflict, reentry, timeout, Ctrl-C, data-release guard time and final input cleanup. The endpoint model is not electrical/CDC validation or a replacement for real FPGA testing. PARLIO/GDMA/DDR3 remain unimplemented.

### Next hardware steps (not executed)

1. Coordinate the prepared P4 firmware replacement with the user. Keep the unverified gate disabled for initial boot/input/ownership diagnostics.
2. Confirm Dock revision, header orientation, measured 3.3V/GND, SW0 mapping, 330-ohm resistors and all eleven signal endpoints against the carrier wiring guide. Do not connect power rails; common GND only. Measure Tab5 boot/idle GPIO2..5 and other bus signals before connecting.
3. Reload the reviewed FPGA SRAM image with SW0 OFF; it was lost on the reported power cycle. Confirm D-line Hi-Z/READY and both boards powered; do not ARM automatically.
4. Record verified profile, reviewed source/artifact hashes and measurements, then build the explicitly enabled profile and coordinate flashing. With user-confirmed wiring and SW0 OFF-to-ON, run ID first, then the 513-exchange suite. Record wiring length, clock/turnaround measurements, CRC results and cleanup behavior separately from mock tests.

Prepared build/evidence: [audit record](validation/dock-gpio-preparation-2026-11-03.json), [artifact snapshot](resources/prepared-dock-gpio.json). Compared with the last recorded firmware, BIN +1776 B and static RAM unchanged. This is the disabled-profile build; the optimizer may discard unreachable physical transport. Enabled-profile resource usage remains unmeasured. The deployed-current snapshot is intentionally unchanged.
