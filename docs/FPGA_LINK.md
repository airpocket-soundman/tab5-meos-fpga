# FPGA LINK module: Dock jumper bringup-v0

For the `fpga_diag` bundle, `fpga features`, LINK/GFX suite and separate NPU CPU
conformance, see [FPGA_NPU.md](FPGA_NPU.md). The reviewed carrier does not yet
provide external NPU/MEM commands or bitstreams; those are reported as blocked,
not silently counted as successful hardware tests.

## Optional GFX module (experimental Dock GFX v0)

Select `-DM5OS_MODULES=fpga_gfx` to include GFX; its `fpga` dependency is added
automatically. Add other modules to the comma-separated selection as needed.
Both modules remain off by default and outside the named size profiles.
With only `fpga`, GFX command/service/model sources are excluded.

GFX follows the carrier's **uncommitted** `codex/dock-jumper-gfx` development
contract. Carrier main `e5a386d` does not contain this extension. The exact
specification, codec and renderer hashes are pinned in
[the carrier review](FPGA_CARRIER_REQUIREMENTS.md) and the independent golden
fixture `tests/fixtures/fpga_dock_gfx_v0.json`. Use a matching
`tab5_dock_gfx_top` bitstream from that carrier worktree. The LINK-only bitstream
returns `unsupported`; `ID=TP` alone does not identify GFX support. Future carrier
changes require a fresh comparison; this is not a released graphics/DDR ABI.

```text
fpga --dock dock-jumper-v0 gfx info
fpga --dock dock-jumper-v0 gfx clear 0x0000
fpga --dock dock-jumper-v0 gfx pixel 31 0 0x001f
fpga --dock dock-jumper-v0 gfx rect 1 2 9 5 0xf800
fpga --dock dock-jumper-v0 gfx read 1 2
fpga --dock dock-jumper-v0 gfx test
fpga --dock dock-jumper-v0 gfx demo
fpga --mock gfx test
fpga --mock gfx demo
```

The device image is 32x32 RGB565 (2KiB of FPGA internal RAM). Coordinates are
x right/y down, origin top left. Colors are 16-bit values, e.g. red `0xf800`,
green `0x07e0`, blue `0x001f`; low byte goes first on the wire. `color RGB565`
also sets the device drawing color directly. `clear RGB565` sets color and
clears all pixels, resetting the rectangle origin. `pixel` and `rect` set their
own color; `rect` also sets its origin. Width/height must be positive and fully
inside the image; clipping is not provided. CLEAR must finish before drawing
or reading. A remote device reset loses initialization and requires CLEAR again.

Each command holds one IO session throughout its color/origin/draw/read steps.
Image state stays in the powered, armed FPGA across successful physical commands.
Each `--mock gfx` command creates a fresh software device; use `test` or `demo`
for a complete mock sequence. `read` reports the returned 16-bit value as JSON
`pixel`; `info` validates the returned dimensions, rather than trusting the ID.

`test` and `demo` overwrite the image with an asymmetric background, red/green
rectangles and blue/yellow/magenta edge points. They perform ID/INFO, drawing and
all 1024 pixel reads (1040 transactions). Every received pixel is compared with
the expected pattern. `demo` then displays **that received buffer** through the
existing LCD driver, up to 16x (512x512); it does not display a CPU-generated
substitute. The physical pins are released before submitting the LCD image.
Only 2KiB is allocated for physical readback; mock mode adds a separate 2KiB
device model. Existing LCD framebuffers remain. Drawing copies the buffer
synchronously before it is freed; frame publication can still time out.

JSON distinguishes `readback_pass` from `lcd_submitted`. The latter means the
display API accepted the image, not a visual hardware check. `backend: mock`
means simulated pixels even when displayed. A mismatch, malformed reply,
unsupported opcode, uninitialized device or transport error stops the sequence;
no automatic retry or display follows a failed readback. Ctrl-C cancels physical
communication. After a physical communication/device error, reset with SW0 OFF
or Key_0 as directed by `reset_required`; a local LCD error alone does not
require FPGA reset. Matching CRC is insufficient to prove a READ pixel is right.

GFX host commands and GPIO-model/LCD-stub tests are implemented; physical GFX
communication and LCD presentation remain untested. No DDR3, HDMI, blit/font,
NPU or PARLIO/GDMA is added by this module.

Run `python tests/test_fpga_gfx.py --cc PATH_TO_ZIG` for the full software suite.
`python tools/sync_fpga_gfx_vectors.py --carrier PATH --check` verifies the
carrier draft hashes and 1,442 stateful oracle vectors; omit `--check` only
after reviewing a deliberate carrier update. The snapshot lets regressions run
without the other checkout. Pin ownership, timing and power prerequisites below
also apply to GFX.

Build the optional Tab5 module with `idf.py -B build-tab5 -DM5OS_MODULES=fpga build`
(`M5OS_BOARD=tab5`). Add `fpga` to a comma-separated module selection to keep
camera/MicroPython/etc. It is off by default and absent from the three published
size profiles. When disabled the command, codec, mock and GPIO backend are all
omitted. Enabling the module alone never configures pins.

## Physical Dock commands

```text
fpga status
fpga --dock dock-jumper-v0 id
fpga --dock dock-jumper-v0 echo 0x55 0xaa
fpga --dock dock-jumper-v0 invert 0x55 0xaa
fpga --dock dock-jumper-v0 test
```

`test` performs ID then ECHO and INVERT for all 256 byte values paired with
their complement (513 exchanges, including walking bits). Every response is
checked for CRC, sequence, framing and expected data. Ctrl-C cancels the active
command. A command claims all 15 pins atomically for its whole duration and
releases them at exit; another IO owner causes a busy result before activation.
`status` only reports software capability; it sends nothing and does not detect
the FPGA. JSON `backend: dock-gpio` identifies an actual GPIO attempt, whereas
`backend: mock` is software only. A successful build is not a hardware test.

Use the LINK bitstream from carrier main
`e5a386d263d8911a490922cc2770292699a87080`, with the Dock wrapper/CST described
in its [Dock wiring specification](https://github.com/airpocket-soundman/tab5-tang-primer20k-carrier/blob/e5a386d263d8911a490922cc2770292699a87080/docs/Dockジャンパ試験_配線とFPGA構成_2026-10-28.md).
The wrapper/top and bitstream must be built in the carrier project. Record the
actual bitstream SHA-256 separately; none was generated or loaded by this change.
The same ID/ECHO/INVERT subset also exists in the uncommitted dock-gfx-v0 draft;
GFX commands require the additional `fpga_gfx` module described above.

| Signals | Tab5 GPIO | Dock |
| --- | --- | --- |
| D0..7 | 18,19,5,3,4,2,48,47 | J12: 9,11,12,10,8,7,6,5 |
| CLK | 16 | J14-10 (T7) |
| READY | 45 | J5-5 |
| DIR | 51 | J5-6 |
| AUX0..3 (reserved inputs) | 17,52,6,7 | J5:7,8,9 / J14:8 |

Follow the source specification's wiring procedure: verify header power pins,
use 220..330-ohm series signal resistors, <=15cm wires, common GND at each
header, and separate supplies without joining 3.3V/5V rails. Remove the Dock
camera/LCD/microphone/touch connections. Disable pad JTAG on GPIO2..5 and USB FS
on GPIO19; COM.X must be absent. Check startup pin states on the actual Tab5.
Connect with both boards powered and SW0 OFF; confirm D inputs before SW0 ON.
The command does not operate SW0, prove power safety or enable the carrier PCB.

The native backend uses MeOS IO ownership/GPIO and the monotonic HAL timer,
without FreeRTOS. CLK is host-driven: high/low >=100us, data setup >=100us
(write pulses thus <=3.34kHz, read pulses <=5kHz). D is released before DIR=1,
with 200us on each side of that switch; DIR=0 settles for 200us before output.
Each exchange has a 100ms deadline; READY polling also has a finite bound if
the timer stalls. Every completed exchange returns D/CLK/DIR to input; AUX and
READY always remain inputs. A failed session refuses further transactions.
On timeout/CRC/sequence/cancellation errors no automatic retry occurs: reset
the FPGA with SW0 OFF or Key_0 before another command. `reset_required` reports
this requirement. Disconnect signal wiring before powering down either side.

No DDR3 or PARLIO/GDMA is implemented. Optional GFX provides the small diagnostic
renderer/readback described above. Hardware speed,
pin voltage, startup/backfeed and FPGA timing remain unverified. See the
[carrier review](FPGA_CARRIER_REQUIREMENTS.md) for the pinned evidence.

## Software endpoint and codec regression

`fpga --mock id`, `fpga --mock echo 0x55 0xaa`, `fpga --mock invert 0x55 0xaa` and `fpga --mock selftest` explicitly select a software endpoint. No GPIO, PARLIO, DMA, DDR3 or electrical timing is exercised by these mock commands.

The independent codec uses 8-byte frames and CRC16/CCITT-FALSE. ID, echo and inversion responses validate framing, sequence, status and data. Transport callbacks support fragmented progress, deadlines, cancellation and cleanup. A bounded poll count also terminates a stalled clock. Clients require owner serialization; the reentry guard is not a cross-task lock. Backends must honor the deadline and buffer contract, including bounded finish/cleanup.

583 golden vectors from GPU revision 3b1ace3d91c6305ec0e32378f8819fce208c2aa7 matched response bytes and acceptance. Fixture provenance/hashes are in tests/fixtures/fpga_bringup_v0_source.json. Eight fragment sizes passed 4104 pattern exchanges and 96 fault checks; additional tests cover corruption, late completion, cancellation, sequence wrap, recursive entry and CLI rejection. Native Tab5 mock selftest passed 513 pattern exchanges and 12 fault checks, including ten additional successful recovery exchanges. Heap usage was unchanged. This validates software execution on Tab5, not FPGA connectivity.

Historical software evidence: [host](validation/fpga-link-host-result.json), [Tab5 mock](validation/fpga-link-hardware-result.json). Run `python tests/test_fpga_link.py --cc PATH_TO_ZIG`.
For the actual GPIO backend with a pin-level device model, run
`python tests/test_fpga_gpio.py --cc PATH_TO_ZIG`. This checks all 513 exchanges,
pin conflicts, electrical-direction ordering, setup/turnaround delays, CRC
failure, timeout, cancellation before/during transfer, stalled time and cleanup,
plus the physical command parser. It does not replace an RTL or electrical test.

## 2026-11-03 integration and hardware gate

The remote bcaf737 already supplied a more complete GPIO session/CLI and GFX/NPU/module work. That implementation is retained; the earlier local one-shot `--gpio --arm dock-v0` prototype is superseded. Use the documented `--dock dock-jumper-v0` commands after review, not that obsolete spelling. AUX pins are input-only and claimed together with the eleven active pins (15 total). The session keeps ownership through the diagnostic sequence, returns all signals to input after each exchange, and closes ownership at command exit. Failed sessions refuse further exchanges and require FPGA SW0/Key0 reset.

An additional compile-time electrical-review gate now defaults OFF: `M5OS_FPGA_DOCK_VERIFIED`. Even with the optional `fpga` module enabled, an unverified build rejects `fpga_gpio_open()` before IO activity. Test harnesses explicitly enable a simulated profile; this never authorizes real pins. `fpga status` reports `profile_verified`. The CMake ON switch is reserved for a subsequently documented physical review; do not enable it automatically. Selecting a physical backend in a command is the explicit host request; FPGA SW0 is a separate manual action.

No P4 flashing, jumper connection or physical ARM was performed in this session. COM4 remains available. Before flashing, show the selected module list and impact to the user. Then, with user coordination, verify Dock revision/header orientation/voltages, resistors, all endpoints, boot/JTAG pad states and SW0 OFF. Reload the lost SRAM image on the FPGA side. Keep power rails separate and use common GND per the carrier guide. Only after recording the verified profile should an enabled build and the ID-first, 513-exchange trial proceed.

Prepared build records created before integrating bcaf737 are historical, **not measurements of the integrated image**. See the preparation audit for the final integration outcome. The existing deployed-current resource snapshot remains unchanged.

Latest wiring correction: the reported physical J5 is 1/2=3.3V, 3/4=GND; do not use the old saved schematic GND pair. Follow the [second remote review](FPGA_CARRIER_REQUIREMENTS.md) and measure the actual headers before wiring. The FPGA top/SSPI packer setting must also be reconciled on the carrier side.

Integrated build passed with ESP-IDF 5.4.4 and `M5OS_MODULES=fpga`, review gate OFF. [Artifact/resource snapshot](resources/integrated-dock-gpio.json). This minimal firmware excludes other optional features and has not been flashed. The merged default/full build failed in existing camera ISP LL calls with the available 5.4.4 SDK; the remote validation used 5.5.2. Do not treat the minimal pass as a full-feature build pass.
