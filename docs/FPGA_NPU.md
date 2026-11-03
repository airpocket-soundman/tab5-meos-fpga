# NPU conformance and jumper all-function diagnostics

2026-10-28. Carrier main reviewed: `e5a386d263d8911a490922cc2770292699a87080`;
the local Dock GFX development tree was inspected too. Source hashes and the
physical scope are recorded in [FPGA_CARRIER_REQUIREMENTS.md](FPGA_CARRIER_REQUIREMENTS.md).

## Current implementation boundary

The reviewed carrier has Dock LINK/GFX bitstreams. Its NPU MAC, requantizer and
dot-job RTL primitives exist, but there is **no NPU jumper command dispatcher,
external job protocol or NPU bitstream** in that tree. The device configuration
document describes a future architecture, not an implemented external API.
No speculative NPU opcode is added to the MeOS link codec.

| Function | MeOS test now | External FPGA test readiness |
| --- | --- | --- |
| ID/echo/invert | `fpga --dock dock-jumper-v0 test` | host backend available; hardware not verified here |
| GFX color/clear/point/origin/rectangle/read | `fpga ... gfx test`, `gfx demo` | matching generated Dock GFX bitstream required |
| INT8 MAC, requantization, dot | `fpga --cpu npu selftest` | NPU dispatcher/protocol/bitstream missing |
| GEMM and NCHW convolution | CPU arithmetic goldens | no integrated external NPU job engine |
| CPU polygons/fractals/particles/etc | `gfxbench suite float` / `integer` | independent CPU graphics workloads |
| DDR3 BIST/read/write, RAMdisk | inventory reports unavailable | memory protocol/controller integration missing |
| FPGA polygon/Z, blit/glyph | inventory reports unavailable | current Dock GFX v0 exposes none of these |
| HDMI and PARLIO/GDMA | inventory reports unavailable | separate output/transport work; no implied validation |
| WholeBody/ONNX inference | not implemented | tensor/model execution is not supplied by primitive arithmetic |

`NPU arithmetic passed` therefore means CPU agreement with the carrier oracle,
not successful FPGA computation, DDR3 access, inference or RTL execution.

## Modules and commands

`fpga_npu` is an optional Tab5 module depending on `fpga`. It includes the native
CPU arithmetic library, golden tests and commands. `fpga_diag` is the convenient
all-test bundle: it adds `fpga_gfx`, `fpga_npu`, `gfxbench` and their dependencies.
Both are off by default and excluded from the named firmware size profiles.

```text
M5OS_BOARD=tab5 idf.py -B build-tab5 -DSDKCONFIG=sdkconfig.tab5 -DM5OS_MODULES=fpga_diag build
```

On PowerShell set `$env:M5OS_BOARD='tab5'` before running `idf.py`. To keep other
optional features, include them in the comma-separated module selection.

```text
fpga features
fpga --cpu npu selftest
fpga --cpu npu selftest mac
fpga --cpu npu selftest requant
fpga --cpu npu selftest dot
fpga --cpu npu selftest gemm
fpga --cpu npu selftest conv
fpga --cpu npu requant -3 1 1 0 -128 127
fpga --cpu npu dot 0 0 1 0 0 -128 127 2 3
fpga --mock suite
fpga --dock dock-jumper-v0 suite
```

`requant` accepts ACC MULT SHIFT OUTPUT_ZERO LOWER UPPER (signed decimal).
The first example returns -2 (negative half ties round away from zero).
`dot` accepts BIAS INPUT_ZERO MULT SHIFT OUTPUT_ZERO LOWER UPPER, followed by
0..16 activation/weight pairs. The example returns 6. Empty dot returns the
requantized bias. Decimal INT32 limits are checked before conversion; samples
and zero points must be signed INT8. `--cpu` is required for these reference
commands so software arithmetic cannot be mistaken for a physical endpoint.

`fpga --dock dock-jumper-v0 npu test` explicitly reports `blocked`, with
`gpio_activated:false`, and exits 2. It does not probe or configure any pins.
It will need a carrier NPU wire implementation before physical submission is
enabled. ID `TP` is not an NPU capability query.

## What the suite reports

`fpga features` is a **host implementation inventory**, not a live FPGA probe.
`--mock suite` runs the software LINK/GFX endpoint plus CPU NPU conformance.
It returns 0 for successful software validation and still reports
`full_hardware_coverage:false`.

The physical suite reserves all 15 Dock pins for the complete LINK/GFX sequence,
runs 513 ID/echo/invert exchanges, then 1040 GFX transactions including all 1024
pixel comparisons. It releases the pins before running CPU NPU tests. Any link
failure stops later work, releases ownership, and reports reset requirements.
No LCD image is submitted by the suite; run `gfx demo` separately for display.

Successful currently available physical tests return `result:partial`, exit 2,
and a `blocked` list for physical NPU, memory, advanced graphics, HDMI and DMA.
This is intentional: missing functions must not become an all-features PASS.
A failed LINK/GFX or CPU conformance check returns 1. Physical NPU is never
silently replaced with CPU and reported as hardware success; the separate CPU
result is explicitly labelled `npu_backend:cpu-reference`.

The suite tests the currently loaded bitstream only. It does not program or
switch FPGA images. The carrier plans separate GFX/NPU/MEM variants, so a future
complete hardware campaign must load the appropriate images between phases and
record each hash. Follow [FPGA_LINK.md](FPGA_LINK.md) for wiring/SW0/power rules;
use carrier-owned programming tools for actual image loading.

## Arithmetic/API scope

`drivers/fpga/fpga_npu.h` provides CPU reference APIs for:

- MAC reset/begin/add/end, with checked INT32 partial sums, latched overflow and
  explicit reset recovery; overflow never wraps or saturates.
- Scalar requantization: signed INT64 product, absolute rounding with half ties
  away from zero, output offset, then clamp. Shift 0..62, multiplier 0..INT32_MAX;
  ReLU is represented by the supplied lower clamp/zero point.
- Dot jobs up to 4096 signed INT8 pairs with optional cancellation/deadline.
- Row-major MxK by KxN GEMM (M,N<=16; K<=256), per-channel bias and input offset.
- Group-1 NCHW/OIHW convolution with stride, asymmetric padding, dilation and
  multiple batches/channels. Batch<=4, channels/outputs<=8, input spatial<=16,
  kernel<=5, stride/dilation<=4, each pad<=8, output<=4096 elements. Padding equals
  the input zero point; reduction order is channel/kernel-row/kernel-column.

Tensor/matrix capacity and configuration are checked before output is modified.
Inputs and outputs must not overlap. Runtime overflow/cancel/timeout can leave
partial matrix/tensor output: discard it entirely on a nonzero return. Scalar
dot/requant results are changed only on success. No heap, FreeRTOS, DMA or GPIO
is used by the arithmetic functions. Work is bounded even without a timer.
These C APIs are not an invented external NPU wire ABI.

## Validation and carrier follow-up

The frozen carrier Python oracle supplies 460 requant, 87 dot, 12 GEMM and
8 convolution jobs (567 total), including signed extrema, all shift values,
half ties, saturation/ReLU, zero/max multipliers, empty dot, reduction overflow,
padding/dilation/stride and invalid parameters. Thirteen MAC lifecycle/recovery
checks run separately. No model weights or inference runtime are implied.

```text
python tests/test_fpga_npu.py --cc PATH_TO_ZIG
python tools/sync_fpga_npu_vectors.py --carrier PATH_TO_CARRIER --check
```

Goldens are frozen in `drivers/fpga/fpga_npu_vectors.h`; source/header hashes are
in `tests/fixtures/fpga_npu_source.json`. The sync tool only reads the carrier
tree and does not generate or change RTL. Host GPIO-model tests exercise suite
reporting, error cleanup and refusal to activate physical NPU without a contract.
Hardware NPU testing remains pending, regardless of CPU/build success.

For actual jumper NPU/MEM tests the carrier must provide:

1. A committed/versioned external command specification and capability query,
   including payload chunking, byte order, signed fields, lengths, sequencing,
   result/error framing, timeouts and reset/recovery.
2. A Dock NPU top/bitstream integrating the arithmetic primitives with that
   dispatcher, checked pin constraints, source/artifact manifests and test vectors.
3. Separate memory/DDR3, inference/model, advanced graphics and HDMI test
   contracts where those functions are claimed. RAM capacity alone defines none
   of those APIs or power/throughput guarantees.

Once a newer carrier implementation is identified, compare it against this CPU
oracle and add the physical transport against that exact version. Do not modify
the MeOS opcode map based on the future device-plan table alone.
