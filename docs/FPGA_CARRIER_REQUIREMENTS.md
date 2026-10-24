# Tang Primer 20K carrier integration review

Date: 2026-10-24. Status: design requirements; no FPGA link, DDR3 access or PARLIO DMA verified on hardware.

## Conclusion / 結論

Tang Primer 20K Core（128 MiB DDR3）と専用キャリアをMeOS GUIの前提にする。現在の8bit案は利用候補になるが、双方向通信、起動時の出力制御、MeOS向けPARLIO/GDMA実装を確認するまで接続互換・速度・RAM削減を保証しない。FPGA RTLとMeOS側ドライバは本プロジェクトで開発する。

共通ハード仕様の起点は [tab5-tang-primer20k-carrier](https://github.com/airpocket-soundman/tab5-tang-primer20k-carrier)。今後FPGA・GUI・GPIO・表示経路を変更する作業の開始時と仕様確定前に最新リモート版を確認し、参照SHAと差分を本記録へ追記する。これは開発時の確認手順であり、無人の定期監視ジョブは作成していない。

## Reviewed versions and evidence

- Carrier remote main: `bb4d589319d8ff44504d12a866ad4b805c7c60f6` (fetched 2026-10-24).
- Carrier local checkout: `0ff1502`, README modified and AGENTS.md untracked; preserved without updating/overwriting the checkout. Review used `git show origin/main:<path>`.
- Linux companion: `c1cd5e4`; GPU companion: `3b1ace3`. Their requirements are separate from MeOS compatibility.
- Reviewed carrier files: common hardware specification, `docs/キャリア基板_企画書.md`, `kicad/gen/design.py`, `kicad/core_pin_assignment.csv`, `docs/シミュレーション再監査_6450c42_開発引継ぎ.md`.
- MeOS: `drivers/io/io.c`, `docs/IO_IMPLEMENTATION_PLAN.md`, `docs/DEVELOPMENT_GOALS.md`; installed ESP-IDF v5.4.4 PARLIO low-level headers are implementation references.

Carrier README/common outline still says schematics/PCB are not created, but remote main contains generated KiCad design and later audit records. Use the actual artifact revision and audit scope, not README status alone. Carrier audit targets `6450c42`; it reports unresolved HDMI ESD continuity and 499-ohm branch corrections. Its numerical reproduction is not electrical hardware validation or permission to manufacture.

## Interface candidate

| Function | P4 GPIO | Carrier nets |
|---|---|---|
| D0..D7 | 18,19,5,3,4,2,48,47 | M_D0..7 / F_D0..7 |
| clock | 16 | M_CLK / F_CLK |
| ready/interrupt | 45 | M_RDY / F_RDY |
| direction | 51 | M_DIR / F_DIR |
| auxiliary candidates | 17,52,6,7 | M_AUX0..3 / F_AUX0..3 |

These are the existing carrier assignments, not new verified wiring. COM.X must be absent; PAD JTAG must not take GPIO2..5. Tab5 internal SD, C6, camera and USB use other signals in the current MeOS assignment, but complete board-revision/netlist checking remains necessary.

The generated `core_pin_assignment.csv` differs from the initial literal mapping in `design.py`: the generator reorders bus pads for layout. For example the CSV places F_D0 on SODIMM77, F_RDY75 and F_DIR119. Do not copy the initial Python dictionary into RTL constraints. Freeze a single exported mapping from the final netlist, identify Core FPGA package balls and bank voltages, and generate/check CST constraints against it.

## Requirements to resolve before carrier fabrication

| Priority | Request | Acceptance evidence |
|---|---|---|
| Required | Bidirectional 8bit transfer, with defined bus master, clock source in both directions, sampling edge, setup/hold and turnaround | Timing diagrams and P4 PARLIO RX/TX feasibility demonstration; include host-only clock versus FPGA-generated return clock choice |
| Required | A way to identify transaction boundaries/valid cycles and apply backpressure | Define how READY/DIR/clock or auxiliary lines implement valid, request, length and pause. One READY line cannot be assumed to independently mean FIFO credit and completion IRQ without a protocol |
| Required | No simultaneous drive or powered-off backfeed | Default input/Hi-Z states, DIR bias, dead cycles, power sequencing and measured startup/reset behavior. Series33-ohm resistors alone do not prove power-off isolation; select a suitable partial-power-down buffer/switch if required |
| Required | Recovery without rebooting Tab5 | Reserve an auxiliary function for FPGA reset/recovery or a proven independent reset path; distinguish soft logic reset, configuration reload and DDR reinitialization. Specify safe defaults before configuring firmware pins |
| Required | Reachable programming/debug and measurement points | Core JTAG connector clearance; accessible D0..7, CLK, READY, DIR, GND, 3V3 and5V; preserve P4 console GPIO37/38 in baseline8bit mode |
| Required | Final pin/voltage contract | Export P4 GPIO, M5-Bus pin, net, SODIMM pin, package ball, bank/VCCIO, direction, reset state, optional resistor state. Check schematic/PCB/CSV/RTL consistency |
| Required | Preserve Tab5 peripheral access | microSD, USB-A hub, touch, C6 Wi-Fi, camera and keyboard remain available mechanically and electrically; reserve FPGA pins through the common IO owner service before enabling the link |
| Required | Power and thermal margin | Evaluate Core+DDR3+HDMI consumption during memory test/rendering, DC-DC temperature, voltage drop, reset/brownout and one-side power-off behavior |
| Conditional | Spare control lines/footprints | Keep AUX0..3 flexible until RX clock/VALID/READY/reset roles are proven. Suggested roles are alternatives, not four already allocated functions |
| Deferred | 16bit mode | M5-Bus pin count cannot support16data+CLK+READY+DIR as currently proposed. Preserve GPIO35 BOOT and37/38 console; do not claim jumpers alone solve safe boot or extra signal demand |
| Conditional | External HDMI output | Useful direct FPGA display path, not a prerequisite for Tab5 internal LCD GUI. Resolve the recorded ESD/branch defects before enabling the HDMI fabrication target |

No new memory chip or FPGA-to-Tab5 MIPI routing is required by this proposal. DDR3 already exists on Core. Carrier HDMI does not automatically drive Tab5's internal MIPI LCD.

## MeOS and RTL contract

The ESP-IDF high-level PARLIO driver must not be assumed directly usable: this MeOS build disables the FreeRTOS scheduler. Implement a native HAL/LL PARLIO+GDMA service with MeOS waits/completion, bounded timeouts, cancellation and cleanup. DMA buffers must meet alignment, reachable-memory and cache-coherency requirements. Use bounded transfer buffers; avoid allocating another full-size frame merely for transport.

Before activating a carrier profile, acquire every FPGA pin atomically through the common IO service and deny shell/MicroPython reassignment. Currently the pins remain general external IO; no FPGA ownership or permanent carrier reservation is implemented. When inactive or failing, return signals to the agreed safe state. Do not enable a permanent reservation based only on this document.

RTL first provides device ID, protocol version/capabilities, DDR size, self-test status, request IDs, bounded READ/WRITE, CRC, errors, FIFO levels and reset/recovery. Then add rectangle fill, overlap-safe copy, glyph/image upload, clipping, dirty-region readback and completion fences. Define pixel format, stride, byte order, coordinates, length limits and DDR region ownership. Japanese fonts may load from SD into a named DDR region; no RAM-disk filesystem is required for glyph lookup.

DDR controller, command transfer and display readers need explicit arbitration and clock-domain crossing. Check available LUT/BSRAM/DSP/timing after synthesis;128 MiB capacity alone does not establish renderer feasibility. Tests cover malformed commands, out-of-range DDR access, incomplete transfer, FIFO overflow, cancellation, and reset during transfer.

## Display and resource budget

Initial display route: FPGA DDR3 -> P4 transfer buffer -> existing P4 MIPI LCD. Keep the working display driver; direct FPGA HDMI is an optional separate output. P4 display buffers remain unless the new display path proves that they can be removed.

At1280x720 RGB565, one frame is1,843,200 B, two3,686,400 B. Full-frame30fps readback needs55.296 MB/s before protocol overhead, exceeding8bit40MHz SDR's40 MB/s theoretical ceiling. Full-frame60fps needs110.592 MB/s. Therefore native-resolution continuous full-frame animation is not a baseline promise.

Start with dirty rectangles and FPGA-local assets/scroll copies. A640x360 RGB565 frame at30fps needs13.824 MB/s payload and is a useful bandwidth test, not a guaranteed target. Measure both directions, switching overhead, USB/Wi-Fi load and LCD latency. Record P4 internal/PSRAM idle/peak and maximum contiguous free space using `mem --json`, plus FPGA DDR allocation and synthesis resource/timing reports. FPGA memory is explicitly accessed storage, not P4's transparent heap.

## Implementation sequence

1. Freeze versioned electrical pin table, clock/handshake timing and recovery contract with the carrier project.
2. Develop synthesizable link RTL and MeOS native transfer driver; simulation covers contention, timeouts and malformed messages.
3. On hardware: low-speed identity/loopback, DDR test, CRC read/write, then10/20/40MHz evaluation. Frequency is an experimental sweep.
4. Implement fill/copy/font commands and dirty rectangles; compare CPU and FPGA including transfer and LCD presentation.
5. Integrate GUI only after resource, cancellation and peripheral-concurrency checks pass. Publish measured limits and update the shared carrier specification if a physical change is needed.

## Review update: carrier second report (2026-10-24)

Reviewed carrier remote main 7929491 after fetching. The previous ESD continuity defect has been corrected in copper; DRC and all eight continuity checks passed, and removing the eight straps caused all eight carrier paths to fail as expected. Remaining concerns include ESD land-pattern matching, omitted socket escape geometry, collapsed P/N branch taps and joint sensitivity corners. Full findings and executed 2D/3D calibration attempts are in [the second-report review](CARRIER_R2_REVIEW_2026-10-24.md). The native MeOS PARLIO clock/handshake, power-off isolation and recovery requirements above remain unverified. No carrier hardware files were modified.

## Review update: carrier third report (2026-10-24)

Reviewed remote main 91dc3b42a8ee5e1ef7b48def237f4a86175f5239. Prior escape-path, separate P/N branch and combined-sensitivity requests are implemented; DRC, positive/negative continuity, numerical reproduction and manifest verification passed. Additional checker gaps were reproduced with altered placed-pad dimensions and trace width; the D2 RGB-channel description also needs correction. See [third-report audit and explicit remediation instructions](CARRIER_R3_AUDIT_HANDOFF_2026-10-24.md). 3D calibration and MeOS transport/electrical requirements remain incomplete.
