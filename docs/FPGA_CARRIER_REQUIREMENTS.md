# Tang Primer 20K carrier integration review

## Carrier all-feature plan reinspection: 2026-10-28

At the user's request, fetched all carrier remote branches in both local clones.
Remote main remains `e5a386d263d8911a490922cc2770292699a87080`; no newer NPU/MEM
branch was found. The preserved `D:\GitHub\tab5-tang-primer20k-carrier` worktree
(`codex/dock-jumper-gfx`) now contains additional uncommitted all-feature records:

- `docs/全機能テスト計画.md`, SHA256
  `050650e8c8a7488364ad858d4dc1f1d5c1347e5e24e2650ad5271557dfc1a014`.
- `fpga/validation/feature_matrix.json`, SHA256
  `425a24b3713891d07bc3978caea5808b663d32dcda9ae66538a76516c6b76d7b`.
- `fpga/tools/test_features.py`, SHA256
  `d20c057fb8182aab0c59168e07b4c32fcfae380a3657b324351bf60253dd184d`.

The matrix enumerates 24 features. The latest recorded PC campaign reports
9 partial and 15 not-ready features, with hardware checks not run. NPU MAC,
requant and dot have passed recorded RTL simulation and component synthesis;
the updated NPU README explicitly says these are not yet a host-accessible
NPU bitstream. Component synthesis is not Dock placement/routing or hardware
integration. The Dock builder still accepts only `link`/`gfx`, the common codec
only exposes LINK/GFX opcodes, and the Dock tops contain no NPU dispatcher.
Thus the new all-feature plan does not unblock physical NPU/MEM commands.
No carrier source files were modified and no hardware was accessed in this review.

## NPU / all-function jumper diagnostics review: 2026-10-28

Fetched carrier main `e5a386d263d8911a490922cc2770292699a87080` again and inspected
the preserved dirty `codex/dock-jumper-gfx` tree: Dock top, link dispatcher, C
codec, GFX contract, NPU MAC/requant/dot RTL, compute reference, bitstream build
variants, device plan, host guide, final pin CSV, generator and r4 hardware/audit
record. Pin/power ownership and the 15-pin Dock profile remain unchanged.

The available Dock tops/commands implement LINK and 32x32 GFX only. NPU RTL is
not connected to the jumper dispatcher; NPU/MEM wire commands and bitstreams,
DDR3 BIST/READ/WRITE, RAMdisk, HDMI and advanced polygon/blit/glyph commands
remain unprovided in this reviewed tree. Do not invent opcodes in MeOS or
present CPU reference results as physical FPGA success. An alternate/newer
carrier NPU implementation has been requested from the user.

CPU NPU semantics reviewed (raw SHA256):

| Carrier path | SHA256 |
| --- | --- |
| `fpga/reference/compute_reference.py` | `23d48e8b2959db7b31eb807b522d414202e1268c32912b257b3dbd467ee67588` |
| `fpga/npu/rtl/int8_mac_lane.v` | `07199c3a6c4354c598ab3847ba23c17215d7d84e1d5deb950204dea1fad47c2d` |
| `fpga/npu/rtl/int8_requantize.v` | `12873790ccdc313a3052428f0efec893128f9fddd52f02b319a97e9d31050209` |
| `fpga/npu/rtl/int8_dot_job.v` | `0f985e2b1d8a1e41d2be5bab588fb581900e1a218f0b78ba82e4889f1fef4dc7` |

Before coding: optional `fpga_npu` CPU arithmetic/reference module, with signed
INT8 inputs, checked INT32 accumulation, ties-away requantization and golden
MAC/dot/GEMM/NCHW convolution tests; reject invalid parameters before narrowing.
Explicitly reject physical NPU requests until a carrier contract exists. Add
an optional full diagnostic runner that reports actual LINK/GFX results,
CPU-reference NPU checks and unsupported planned functions separately, never
claiming full hardware coverage while NPU/MEM/etc are unavailable. No RTL or
hardware design changes in this repository.

## GPU LAB native program review: 2026-10-28

Fetched carrier remote main: `e5a386d263d8911a490922cc2770292699a87080`
(unchanged). Rechecked final core CSV, generator, r4 generated hardware/audit
record, and current Dock GFX command specification. GPU LAB source is
`tab5-fpga-gpu d4d89e308b4a8aa799199b20e7d47c96b536e3fd` (`main/lab.cpp`,
`main/lab.hpp`, `reference/cpp/integer_raster.hpp`, benchmark/integer docs).
Its FPGA backend explicitly returns unavailable: the eight 640x360 scenes are
CPU workloads. Dock GFX v0 has no triangle, depth-buffer or frame-upload ABI.

Before implementation: expose optional `gfxbench` module/native shell program,
without FPGA/GPIO dependency; retain 640x360 RGB565 and CPU raster rules, use
MeOS PSRAM allocation, input, waits and existing synchronous LCD drawing.
Replace C++ dynamic containers on firmware paths with explicit checked MeOS
allocation; import no LVGL, BSP, USB protocol, FreeRTOS task or allocator APIs.
No pin, electrical, power, transport, FPGA RTL or LCD-driver changes. Existing
LCD buffers stay. Report CPU-only measurements, not FPGA speedups. FPGA
polygon offload remains a future carrier command/bitstream requirement.

## GFX host implementation review: 2026-10-28

Fetched carrier remote main again: `e5a386d263d8911a490922cc2770292699a87080`
(unchanged). Rechecked final core CSV, generator CLK assignments and r4 generated
hardware/audit record. No pin, power or electrical changes are required. Existing
dirty `D:\GitHub\tab5-tang-primer20k-carrier` checkout is preserved.

GFX is still an **uncommitted experimental source contract**, not contained in
that main SHA. Adopted files from its `codex/dock-jumper-gfx` worktree (raw SHA256):

| File | SHA256 |
| --- | --- |
| `fpga/link/DOCK_GFX_V0.md` | `6b6cbc2eca820e204ec40151c37cdc7a934e14b6647ec4ea15c739ecfd8982bb` |
| `fpga/host/link_protocol.h` | `bfc5a9a6970789f84b07864ad74acf30242b399d607ae02cb7901f901349eb57` |
| `fpga/gfx/rtl/gfx_bench.v` | `841a4f50baa9e3200bbda1cd61bc9511db36909f76e854a9b66b331ebda2c392` |

Also inspected the host software guide, Dock CST, GFX RTL and validation record.
The latter reports OSS synthesis/pack and RTL successes, but no hardware test;
these are carrier-reported results, not newly executed MeOS validation.
Requirements before coding: optional `fpga_gfx` module depends on `fpga`; current
15-pin Dock profile and timing unchanged; commands 0x10..16 only; 32x32 RGB565 in
FPGA internal 2KiB RAM, not DDR3. Reserve IO ownership across COLOR/ORIGIN/RECT
and complete readback. Check CRC/sequence before classifying status2/3/5; never
retry unknown completion. Compare all 1024 received pixels before displaying
that received buffer via the existing synchronous LCD drawing API. Keep LCD
buffers and current peripherals. Hardware GFX needs the matching carrier GFX
bitstream (`tab5_dock_gfx_top`); ID alone does not prove GFX capability.

## Dock GPIO implementation review: 2026-10-28

Fetched carrier origin and reviewed main `e5a386d263d8911a490922cc2770292699a87080`:
Dock jumper wiring document, `fpga/link/rtl/link_bringup.v`, Dock CST,
`kicad/core_pin_assignment.csv`, generator, and `spark/single-sided-r4/README.md`
with its generated-hardware/audit status. The existing dirty checkout at
`D:\GitHub\tab5-tang-primer20k-carrier` was preserved. A separate clone is under
the required owner directory. Carrier CLK K14/SODIMM52 and Dock CLK T7 are
different profiles; this implementation enables **Dock jumper only**.

Supplementary uncommitted `codex/dock-jumper-gfx` host guide and DOCK_GFX_V0
were inspected for GPIO timing/cleanup guidance, but are not a pinned released
GFX contract. This change implements only committed LINK ID/ECHO/INVERT.
No RTL, carrier hardware, GFX, DDR3 or PARLIO/DMA changes are made here.

Implementation requirements recorded before coding: opt-in `fpga` module;
explicit `dock-jumper-v0` selection; atomic IO ownership of D0..7, CLK, DIR,
READY and unused AUX0..3; no pin activation at boot; READY always input;
D inputs before DIR=1; 200us turnaround; CLK high/low and data setup >=100us;
deadline and cancellation checks; input cleanup on completion/failure.
All 15 pins overlap only external IO in MeOS, and must still pass the runtime
owner check. GPIO2..5 pad JTAG and GPIO19 USB FS must be disabled externally;
the existing USB-A host, console37/38, SD and C6 assignments are preserved.
The Dock requires its documented SW0 ARM, 220..330-ohm signal resistors,
separate supplies and common grounds. Software cannot validate wiring, power
or SW0. Recovery after a failed/partial exchange requires SW0 OFF or Key_0
before another command. Build/mock tests are not electrical validation.

Date: 2026-10-24. Status: design requirements; no FPGA link, DDR3 access or PARLIO DMA verified on hardware.

## Conclusion / 結論

Tang Primer 20K Core（128 MiB DDR3）と専用キャリアをMeOS GUIの前提にする。現在の8bit案は利用候補になるが、双方向通信、起動時の出力制御、MeOS向けPARLIO/GDMA実装を確認するまで接続互換・速度・RAM削減を保証しない。MeOS側ドライバは本プロジェクトで開発する。FPGA RTLとビットストリームは2026-10-28からキャリアリポジトリで開発・管理し、本書のRTL要求はキャリア側デバイス仕様への要求として扱う。

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

Before activating a carrier profile, acquire every FPGA pin atomically through the common IO service and deny shell/MicroPython reassignment. Pins remain general external IO while inactive. The optional Dock GPIO module now claims all 15 pins for each diagnostic session; a carrier PCB profile and permanent carrier reservation are not implemented. When inactive or failing, return signals to the agreed safe state. Do not enable a permanent reservation based only on this document.

RTL first provides device ID, protocol version/capabilities, DDR size, self-test status, request IDs, bounded READ/WRITE, CRC, errors, FIFO levels and reset/recovery. Then add rectangle fill, overlap-safe copy, glyph/image upload, clipping, dirty-region readback and completion fences. Define pixel format, stride, byte order, coordinates, length limits and DDR region ownership. Japanese fonts may load from SD into a named DDR region; no RAM-disk filesystem is required for glyph lookup.

DDR controller, command transfer and display readers need explicit arbitration and clock-domain crossing. Check available LUT/BSRAM/DSP/timing after synthesis;128 MiB capacity alone does not establish renderer feasibility. Tests cover malformed commands, out-of-range DDR access, incomplete transfer, FIFO overflow, cancellation, and reset during transfer.

## Display and resource budget

Initial display route: FPGA DDR3 -> P4 transfer buffer -> existing P4 MIPI LCD. Keep the working display driver; direct FPGA HDMI is an optional separate output. P4 display buffers remain unless the new display path proves that they can be removed.

At1280x720 RGB565, one frame is1,843,200 B, two3,686,400 B. Full-frame30fps readback needs55.296 MB/s before protocol overhead, exceeding8bit40MHz SDR's40 MB/s theoretical ceiling. Full-frame60fps needs110.592 MB/s. Therefore native-resolution continuous full-frame animation is not a baseline promise.

Start with dirty rectangles and FPGA-local assets/scroll copies. A640x360 RGB565 frame at30fps needs13.824 MB/s payload and is a useful bandwidth test, not a guaranteed target. Measure both directions, switching overhead, USB/Wi-Fi load and LCD latency. Record P4 internal/PSRAM idle/peak and maximum contiguous free space using `mem --json`, plus FPGA DDR allocation and synthesis resource/timing reports. FPGA memory is explicitly accessed storage, not P4's transparent heap.

## Implementation sequence

1. Freeze versioned electrical pin table, clock/handshake timing and recovery contract with the carrier project.
2. Develop the MeOS native transfer driver here, and the synthesizable link RTL in the carrier repository (`fpga/`); simulation covers contention, timeouts and malformed messages.
3. On hardware: low-speed identity/loopback, DDR test, CRC read/write, then10/20/40MHz evaluation. Frequency is an experimental sweep.
4. Implement fill/copy/font commands and dirty rectangles; compare CPU and FPGA including transfer and LCD presentation.
5. Integrate GUI only after resource, cancellation and peripheral-concurrency checks pass. Publish measured limits and update the shared carrier specification if a physical change is needed.

## Review update: carrier second report (2026-10-24)

Reviewed carrier remote main 7929491 after fetching. The previous ESD continuity defect has been corrected in copper; DRC and all eight continuity checks passed, and removing the eight straps caused all eight carrier paths to fail as expected. Remaining concerns include ESD land-pattern matching, omitted socket escape geometry, collapsed P/N branch taps and joint sensitivity corners. Full findings and executed 2D/3D calibration attempts are in [the second-report review](CARRIER_R2_REVIEW_2026-10-24.md). The native MeOS PARLIO clock/handshake, power-off isolation and recovery requirements above remain unverified. No carrier hardware files were modified.

## Review update: carrier third report (2026-10-24)

Reviewed remote main 91dc3b42a8ee5e1ef7b48def237f4a86175f5239. Prior escape-path, separate P/N branch and combined-sensitivity requests are implemented; DRC, positive/negative continuity, numerical reproduction and manifest verification passed. Additional checker gaps were reproduced with altered placed-pad dimensions and trace width; the D2 RGB-channel description also needs correction. See [third-report audit and explicit remediation instructions](CARRIER_R3_AUDIT_HANDOFF_2026-10-24.md). 3D calibration and MeOS transport/electrical requirements remain incomplete.

## Checker remediation verified (2026-10-24)

Fetched carrier main and implemented the R3 checker/document fixes through 851adf4. All24 regression cases passed; all55 manifest hashes matched. Electrical design, PCB and channel model are unchanged. [Fix verification](CARRIER_R3_FIX_VERIFICATION_2026-10-24.md) records the commits and bounded completion scope. Carrier transport, power-off behavior, final pin/CST table and MeOS IO ownership remain unverified.

## Review update: 2026-10-26

Fetched and reviewed carrier main 2af05a985cbf572ad954fd8727c42415be8d544b, including the hardware specification, final core pin CSV, generator and single-sided-r4 record. F_CLK moved from Core SODIMM pin80 to pin52 (generator: K14 / IOT30A / GCLKT_1); pin80 is now P2_6. P4 clock remains GPIO16. These source assignments are not electrical validation. r4 removes the main and FPGA-only SW3 switches; there is no carrier external-power switch and Tab5 battery power is not controlled. Power isolation/sequencing and fabrication readiness remain unverified; EasyEDA migration/Spark publication are incomplete.

MeOS now has an explicit software-only bringup-v0 codec/mock diagnostic, with no physical pin allocation. See [FPGA link scope](FPGA_LINK.md). The new BMP shell viewer uses the existing P4 LCD path and changes no carrier signal allocation.

## Review update: 2026-10-28 (FPGA device ownership)

Reviewed carrier `d9a2618ffc33ae84c4fdd98e517cb2517827d13f` (branch `claude/tab5-tang-primer-test-64f0c5`, not yet merged to main): `AGENTS.md`, `README.md`, `docs/リポジトリ分担と参照ルール.md`, `docs/FPGAデバイス構成_ビットストリームと機能.md`, `fpga/README.md`, `docs/Dockジャンパ試験_配線とFPGA構成_2026-10-28.md`.

- The carrier repository now owns the FPGA device: bitstreams split by function (LINK, GFX, NPU, MEM), RTL, constraints, and the link/command specification. GFX, NPU, HDMI and RAMdisk are intended for every host (ESP-IDF, MeOS, NOMMU Linux). This repository keeps the MeOS host driver and GUI integration; the "MeOS and RTL contract" above is a requirement on the carrier device specification.
- Default bitstream plan before measurement: GFX with HDMI and RAMdisk, NPU with RAMdisk, MEM alone; split further if an option costs 5-10% or more performance, fails timing or forces fewer MACs/tiles. GFX output to the Tab5 LCD uses the readback route described above.
- A Dock jumper bench (Tab5 M5-Bus to Dock J12/J5/J14, same P4 GPIO assignment, CLK on T7 GCLKT_3) was documented for pre-carrier tests. The link v0 bitstream is shared by ESP-IDF, MeOS and Linux; MeOS still needs its physical GPIO/PARLIO backend. Nothing in this update was synthesized or tested on hardware.
