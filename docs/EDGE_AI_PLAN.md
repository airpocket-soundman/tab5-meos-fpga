# Edge AI for MicroPython: plan

Status: plan only (2026-10-27). Nothing in this document is implemented. Numbers marked *measure* are deliberately left
open: they depend on hardware that has not been exercised yet.

## Goal

Run small neural networks (digit and gesture classifiers, a small image classifier, later a small detector) from
MicroPython on the Tab5, on camera frames, with two interchangeable back ends:

1. **CPU**: the ESP32-P4, always available, the reference and the fallback.
2. **FPGA** ("the GPU"): the Tang Primer 20K on the carrier, for convolution-heavy layers once the link exists.

The same model file and the same Python code must run on either, so that a model can be developed on the CPU and sped up by
adding the carrier. It is **not** a goal to run large models, to train on the device, or to reproduce a general framework.

## What is known, and what is not

| fact | source / status |
| --- | --- |
| P4 at 400 MHz, 32 MB PSRAM, 192 KB internal arena; MeOS runs on one core | build log, `docs/RESOURCE_USAGE.md` |
| MicroPython GC heap is 512 KiB; tensors must live outside it (PSRAM, kmalloc) and be handed to Python as buffers | `docs/MICROPYTHON.md` |
| Cameras deliver RGB565 frames (built-in 1280x720; USB 352x288 and up), already behind `camera.Camera` | `docs/MICROPYTHON_API.md` |
| FPGA link: 8-bit parallel through PARLIO, 40 MB/s is only the theoretical ceiling of 8-bit 40 MHz SDR; **no link, DDR3 access or PARLIO DMA has been verified on hardware**; `fpga --mock` is a software endpoint | `docs/FPGA_LINK.md`, `docs/FPGA_CARRIER_REQUIREMENTS.md` |
| 128 MiB DDR3 on the Core board; capacity alone says nothing about speed or feasibility | `AGENTS.md` |
| FPGA logic budget (LUT, BSRAM, DSP blocks) and achievable clock | **unknown until synthesis**; take the device limits from the Gowin datasheet and the synthesis report, not from memory |
| Whether the P4's SIMD extension is usable for int8 kernels with the toolchain in use, and how fast | **measure** (phase 0) |

## Architecture

```
 MicroPython   edgeai.load / run / preprocess        (module "edgeai")
      |
 runtime       model file, tensor arena, op dispatch, per-layer profile
      |
 +----+------------------+
 | CPU kernels (C, int8) | FPGA kernels via the link (module "edgeai_fpga")
 +-----------------------+
```

- **Model format** `.m5nn`: a small flat file designed to be mmap-able from the SD card: header (version, op list,
  tensor shapes, quantisation parameters), then weights. int8 weights and activations with int32 accumulators and
  per-channel scales, the scheme TensorFlow Lite uses, so that existing converters can feed it. Whether to read `.tflite`
  directly (parsing FlatBuffers in the runtime) or convert on the host is an open decision; the converter is the cheaper
  start and keeps the runtime small.
- **Operators, in the order they are needed**: conv2d (1x1, 3x3), depthwise conv2d, average and max pool, fully
  connected, add, ReLU / ReLU6, softmax, reshape. Nothing else until a model needs it.
- **Memory**: one arena, planned at load time from the tensor lifetimes (a peak-live-bytes pass), in PSRAM; weights stay
  in the file mapping or are copied once into FPGA DDR3. Python sees tensors as buffers (like `Image.data`), never as
  heap objects.
- **Placement**: per layer, `cpu` or `fpga`, chosen from a cost model that uses measured numbers (below). The user can force
  `cpu`. Activations stay on the FPGA between consecutive FPGA layers; only the input and the final output cross the link.
- **Module system**: `edgeai` (needs `micropython`), `edgeai_fpga` (needs `edgeai` and the FPGA link). Part of the `large`
  firmware profile only until the sizes are known.

## Python API (a sketch, to be settled in phase 1)

```python
import camera, edgeai as ai

net = ai.load("/sd/models/digits.m5nn")        # parse, plan the arena
print(net.input_shape, net.output_shape)       # (1, 28, 28, 1) (1, 10)
net.backend("auto")                            # "cpu", "fpga" or "auto"

cam = camera.Camera("camera0")
img = camera.Image(160, 120)
cam.read_into(img)
x = ai.preprocess(img, net.input_shape, mean=127.5, scale=1/127.5)   # crop/resize/grey/quantise
y = net.run(x)
print(ai.argmax(y), ai.topk(y, 3))
print(net.profile())                           # per-layer: backend, microseconds, bytes moved
```

`preprocess` is a C function (crop, nearest or bilinear resize, grey conversion, quantise) because doing it per pixel in
Python would cost more than the network.

## Cost model

For layer *i*: `T_cpu(i) = MACs(i) / R_cpu`, and for an FPGA layer `T_fpga(i) = max(MACs(i) / R_fpga, bytes(i) / B_ddr) +
T_setup`. Moving data over the link costs `bytes / B_link + T_turnaround`. A layer is offloaded when the time saved
exceeds the transfers it forces; consecutive FPGA layers share the saving because their activations do not move.
`R_cpu`, `R_fpga`, `B_ddr`, `B_link`, `T_setup` and `T_turnaround` are all **measure** items. Typical input sizes to keep in
mind (int8): 28x28x1 = 784 B, 96x96x3 = 27.6 KB, 224x224x3 = 150.5 KB; weights are loaded once.

## Phases

Each phase ends with a measurement or a test that can fail.

| phase | work | done when |
| --- | --- | --- |
| 0. Baseline | Time an int8 3x3 convolution and a fully connected layer in plain C on the P4; check whether the SIMD extension can be used from the build and what it gains; record PSRAM bandwidth under camera and display load | numbers for `R_cpu` and memory bandwidth in `docs/`, with the method |
| 1. CPU runtime | `.m5nn` format, host converter from TensorFlow Lite int8, the operators above in C, arena planner, `edgeai` module with `load`, `run`, `preprocess`, `profile`; models: MNIST-style digits, then a small image classifier | outputs match a host reference (numpy) **bit-exactly** on a test set; a digit read from the camera on the device; a host test in `tests/edgeai` like the JPEG one |
| 2. Link bring-up | The existing FPGA plan: transport, DDR3 round trip, effective bandwidth in both directions | `B_link` and `B_ddr` measured; this phase belongs to the FPGA project, edge AI only consumes it |
| 3. First FPGA kernel | int8 3x3 convolution engine with line buffers and the DSP multipliers, weights in DDR3; one layer offloaded from the runtime | RTL simulation matches the host reference bit-exactly; the same layer on hardware matches the CPU result; `T_fpga` measured against `T_cpu` |
| 4. Placement | Cost model fed by the profile; whole small networks resident on the FPGA; automatic back-end choice | end-to-end speed-up (or an honest statement that there is none) for the phase-1 models |
| 5. Camera pipeline | Crop and resize on the FPGA so that only the network input crosses the link; USB and built-in cameras | per-frame latency from camera to class, measured |
| 6. Larger models | Depthwise convolution, residual adds, a small detector | one detector running at a measured frame rate |

## Testing

- A numpy reference implementation of each operator (integer arithmetic exactly as the device does it), checked once against
  TensorFlow Lite's own results, then used as the golden source for the C kernels (host build with Visual Studio, like
  `tests/mjpeg`) and for the RTL testbenches.
- On-device self-test that runs a fixed tiny network and compares a checksum, usable from `python -c`.
- Every back end must produce identical int8 outputs; a difference is a failure, not a tolerance.

## Risks and open decisions

- The link may turn out too slow or too hard to bring up for per-layer round trips; then only whole-network residency
  (weights and activations on the FPGA, one input and one output crossing) makes sense, and the CPU path stays the product.
- The FPGA budget (DSP blocks, BSRAM, clock after place and route) bounds the convolution engine; do not promise MACs per
  second before synthesis.
- `.tflite` versus `.m5nn` as the runtime format; per-channel versus per-tensor quantisation for the first models.
- Heap pressure: a 512 KiB GC heap cannot hold models, so everything big must stay outside it and be passed as buffers.
- Licensing of any model files and converter dependencies that are redistributed.
- Power and thermal margin of the carrier under sustained FPGA load is a carrier-project item
  (`docs/FPGA_CARRIER_REQUIREMENTS.md`).
