# GPU LAB on MeOS: `gfxbench`

`gfxbench` is an optional native MeOS shell program, adapted from the ESP-IDF
`tab5-fpga-gpu` GPU LAB renderer at `d4d89e3`. It runs inside MeOS; switching to
the ESP-IDF diagnostic firmware is not required. It is compiled into firmware
as a module, rather than loaded from SD as a separate `.m5ex` binary.

## Build and run

In an ESP-IDF environment configured for this MeOS checkout:

```powershell
$env:M5OS_BOARD = 'tab5'
idf.py -B build-tab5 '-DSDKCONFIG=sdkconfig.tab5' '-DM5OS_MODULES=gfxbench' build
```

`gfxbench` is off by default and not part of the small/medium/large profiles.
Add other modules to the selection if needed, e.g.
`-DM5OS_MODULES=gfxbench,fpga_gfx` enables both this CPU program and the independent
FPGA diagnostic commands. The latter adds `fpga` automatically. The program
never allocates FPGA pins, accesses DDR3, or requires a connected Dock.

```text
gfxbench list
gfxbench
gfxbench polygon heavy
gfxbench polygon medium integer
gfxbench wire light integer --bench
gfxbench polygon heavy float --bench
gfxbench suite float --headless
gfxbench suite integer --headless
gfxbench fire light --frames 60
gfxbench flow light --frames 2 --headless
```

With no arguments it shows the animated LIGHT polygon scene. Live mode runs
until Q/Esc/Ctrl-C, unless `--frames 1..10000` limits it. `--headless` omits image
presentation and requires a finite frame count or benchmark/suite. Console
output remains enabled. `--frames` and `--bench` cannot be combined.

## Scenes and controls

| Scene | LIGHT / MEDIUM / HEAVY | CPU work |
| --- | --- | --- |
| `wire` | 16 / 64 / 144 cubes | two-axis rotation, projection, 12 lines/cube |
| `polygon` | 16 / 64 / 144 cubes | 12 triangles/cube, reciprocal-depth Z buffer |
| `mandelbrot` | 24 / 64 / 128 iterations max | full-image fractal zoom |
| `julia` | 24 / 64 / 128 iterations max | animated complex constant |
| `particles` | 512 / 2048 / 8192 | fixed-seed particle vortex |
| `galaxy` | 1024 / 4096 / 16384 | analytic circular Kepler orbits |
| `fire` | 2 / 4 / 6 noise octaves | procedural fire |
| `flow` | 512 / 2048 / 8192 | persistent cylinder wake/trails |

N cycles scenes, L cycles load, R switches raster on wire/polygon, Space pauses,
B starts a benchmark from frame 0, Q/Esc quits. Ctrl-C cancels. A scene/load/raster
change cancels an ongoing measurement. Input is checked between frames, so a
heavy CPU frame must finish before an exit key takes effect. Integer mode only
cycles wire/polygon; other scenes reject an integer selection.

`float` preserves the original legacy raster rules. `integer` uses signed 12.4
screen coordinates, top-left triangle coverage, integer depth interpolation and
strict-greater Z; rotation/projection still use CPU floating point. The two
rasters need not produce identical boundary pixels. Frame number drives
animation, rather than wall time, so equivalent runs process equivalent work.

## Display and memory

The original 640x360 RGB565 image is copied through MeOS's existing LCD drawing
API. Tab5's 1280x720 screen shows it at 1:1 with a status/control overlay. The
program pauses the LCD text console while active and restores its previous
pause state when it exits. A frame batch is published when available; otherwise
normal synchronous drawing is used. A reported presentation failure ends the run.

Color/depth buffers use 921,600 bytes of PSRAM in total. Flow lazily adds a
460,800-byte trail buffer, up to 65,536 bytes of particles and a small state
object. Existing LCD framebuffer memory is additional. There is one program
render buffer because the display API copies it synchronously. No extra full
screen framebuffer is allocated by this program. Allocations are checked and
freed on normal exit, input cancellation and failure; no per-frame STL heap or
FreeRTOS allocator is used. One program session runs at a time.

## Measurements

`--bench` does 24 warmup frames then 120 measured frames and returns to the shell.
B in live mode does the same measurement, prints a result and resumes live
animation. `suite float` runs 8 scenes x 3 loads (24 cases); `suite integer`
runs 2 x 3 (6 cases). Each case restarts at frame 0. An incomplete/cancelled
case produces no successful benchmark row. Completed earlier suite cases remain
in the console log.

```text
GFXBENCH_HEADER,schema,backend,raster,scene,load,width,height,frames,render_mean_us,render_p95_us,pipeline_mean_us,frame_fps_milli
GFXBENCH,1,CPU,integer_g0,polygon,1,640,360,120,...
```

Load is 0/1/2. Times are microseconds; `frame_fps_milli / 1000` is pipeline FPS.
Render includes clearing, geometry and rasterization. Pipeline includes the
render, display/HUD submission if enabled, and a MeOS 1ms sleep (actual scheduler
delay is measured). P95 uses nearest rank (114th of 120 sorted samples).
Headless results and displayed results have different pipeline costs. These
are application timings, not measured LCD scanout FPS. MeOS timing and UI cost
also differ from the ESP-IDF LVGL/RTOS program; compare render time with those
conditions recorded. A final JSON line reports status, rendered frame count,
completed cases and the last image checksum. Results are not persisted to SD.

## FPGA scope and verification

The source GPU LAB's FPGA backend is unavailable. This port is CPU-only too:
`--fpga` is rejected, rather than silently reporting CPU results as GPU results.
The current Dock GFX v0 bitstream exposes a separate 32x32 point/rectangle
diagnostic and no polygon/Z-buffer protocol. Use `fpga ... gfx test/demo` for
that path; see [FPGA_LINK.md](FPGA_LINK.md). Running these 640x360 scenes on the
FPGA will need a carrier-owned command/bitstream extension.

```text
python tests/test_gfxbench.py --cc PATH_TO_ZIG
python tests/test_gfxbench.py --cc PATH_TO_ZIG --reference PATH_TO_TAB5_FPGA_GPU
```

Host checks cover all scenes/loads, original-image/depth/counter equivalence,
reproducibility, buffer guards, both complete suites, statistics, controls,
cancellation, allocation and display failures, and cleanup. The optional
reference comparison checks 60 combinations against the pinned original source
using the same host compiler/math library. It is not a promise of identical
floating-point hashes on Tab5. ESP32-P4 firmware builds are checked separately;
native MeOS boot, LCD animation and hardware performance have not been tested
by this change. Provenance/adaptations: [GPU LAB source notes](../third_party/gpu_lab/README.md).
