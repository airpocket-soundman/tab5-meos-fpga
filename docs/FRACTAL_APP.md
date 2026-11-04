# Standalone animated CPU/FPGA fractal application

## Animation pipeline update — 2026-11-04

Installed at the same128x128 source resolution, enlarged to512x512. The earlier
19x fixed-tile result was not application FPS. GPU Julia playback improved from
about1FPS to5.5–6.3FPS after its first frame; this remains below smooth30/60FPS.
The GPU is not universally faster.

| Frozen phase0, median of five repeats | CPU full frame | FPGA full frame | CPU compute | FPGA including transport | LCD (CPU/FPGA) |
|---|---:|---:|---:|---:|---:|
| Mandelbrot |190ms|170ms|96ms|75ms|93/93ms|
| Julia |160ms|160ms|76ms|69ms|82/88ms|

Use `frame_ms` for the frozen comparison: paused FPS includes idle time.
[Measurements](validation/fractal-animation-final-2026-11-04.json) and
[artifact hashes](validation/fractal-animation-summary-2026-11-04.json) are retained.

Focused input polling removes per-tile10ms waits. A palette lookup, suppressed
fast-frame progress and static window chrome reduce presentation work. `0`
freezes phase0. Slow frames still publish progress. Optional
`DISPLAY_IOC_BLIT_SCALED` exposes existing nearest-neighbor kernel drawing;
older display firmware uses an expanded single-blit fallback. Workspace is now
919596 bytes (about898KiB) in PSRAM; stack remains4KiB. Frames contain actual
computed pixels. No prerecorded images or reduced benchmark resolution.

Identical shared setters are cached only within an exclusive successful GPU
session. Origins, DRAW and validated TILE readback always execute. Pure host
codecs compile with-O2; physical clocks, CRC and failure policies are unchanged.
USB TX writes EP1 directly: the stock LL read/modify/write helper consumed
concurrent RX keys. Full-packet ZLP and unplug bounds remain.

[Hardware functional checks](validation/fractal-animation-functional-2026-11-04.json)
passed both scenes,32/64/128/256 comparisons, controls and cleanup. Heap used
bytes returned to baseline; display errors/timeouts were0.
[Endurance](validation/fpga-animation-session-endurance-2026-11-04.json) passed
1152 tiles and1179648 exact reference pixels. The21 app model cases cover both
legacy and scaled-display paths. Physical LCD appearance was not independently
assessed.


## Communication update — 2026-11-04 (installed and physically tested)

The5732-byte SD app now opens one optional GPU session per complete frame,
renders all32x32 tiles, and closes it before LCD presentation or cancelled
input. An older firmware returning ERR_NOSYS for BEGIN uses the existing
single-tile ABI; a failed physical exchange is never retried or replaced.
GPU milliseconds include BEGIN/END overhead. Per-tile `elapsed_us` in a held
session excludes the separate BEGIN/END; the fixed benchmark also reports
full-case `wall_ms` for fair comparison.

The dedicated host firmware uses GPIO control TX/RX with1us software waits
and continuous5MHz DMA image readback. Bounded GPU ioctls temporarily raise
caller priority while keeping interrupts enabled. This prevents the measured
148ms scheduler gap from exceeding the FPGA100ms no-strobe timeout. The P4
USB console now terminates full64-byte packets with a ZLP, so complete GPU
reports reach the PC without waiting for another key.

The [final UI test](validation/fractal-speed-optimized-2026-11-04.json) passed
all resolutions, both scenes, complete comparisons, pause/resume, GPU playback,
CPU switching during playback and exit with memory restored. The
[endurance test](validation/fpga-device-speed-endurance-2026-11-04.json)
passed1,152 tiles and1,179,648 exact pixel comparisons across72 sessions.
20 host scenarios cover both session-enabled and old-ABI control/cancellation.

A fixed16-tile Mandelbrot benchmark improved from about1.86s to97ms (about19x).
This is communication plus device work, not LCD FPS. See the
[summary and exact artifact hashes](validation/fpga-speed-summary-2026-11-04.json).
Physical display appearance has not been independently assessed.

`apps/m5ex/fpgabench.c` builds an optional finite SD executable. It renders16
identical tiles per case,3 repetitions each of Mandelbrot/Julia, and verifies
all pixels against the CPU reference. `run /sd/fpgabench.m5ex` runs it without
LCD output. `tools/test_fpga_device_hardware.py` captures and checks its reports;
it does not program either board. For repeated tests use `--runs 12` and supply
the installed app/firmware paths and an output JSON path as required by `--help`.

## Previous presentation-only revision

## Presentation update — 2026-11-04 (installed; CPU/FPGA hardware tested)


The standalone `fractal.m5ex` viewer uses a title bar, large status text and
a 512-pixel image area on Tab5. PLAYING, PAUSED and ERROR distinguish animation
state. Before rendering, the screen publishes CPU rendering or FPGA rendering /
waiting, completed tiles, percentage, and elapsed time. Progress is refreshed
between tiles at most every 150 ms; a single blocking FPGA tile cannot update
the display until the device call returns or times out. Q is handled between
tiles. The previous completed image stays visible during computation.

Text is 2x and the title is 3x. The M5EX API currently exposes display ioctls,
not the native window service, so this viewer draws its own frame.

A vivid blue/cyan/yellow/red/magenta palette is applied at presentation time.
CPU/FPGA computation and exact pixel comparisons retain their original colors,
so no different FPGA bitstream is required for this display update.

Controls: C CPU, G FPGA, M Mandelbrot, J Julia, R resolution, V compare,
Space/A play/pause, Q/Escape close.

Build:
```powershell
python tools/m5exbuild.py --arch riscv apps/m5ex/fractal.c -o build-tab5/fractal.m5ex
```

Validation: the 16 host-model scenarios cover pixel presentation, comparison,
animation, resolution changes, cancellation, display/device errors and cleanup.
Title size, large text and progress presence are checked. RISC-V M5EX build
passes. The 5600-byte app was uploaded to `/sd/fractal.m5ex` with CRC32 `6a6bb1f0`
verified. The [CPU hardware record](validation/fractal-studio-cpu-hardware-2026-11-04.json)
confirms animation, pause/idle/resume, both scenes, all resolutions and exit
with internal/PSRAM used bytes restored. Display errors/timeouts stayed zero.
One internal allocation miss is expected from the internal-first allocator
falling back to PSRAM for the workspace. Physical visual quality was not assessed.

The [first FPGA attempt](validation/fractal-studio-hardware-2026-11-04.json)
failed with ERR_IO (-6); ID returned eight FF bytes and CRC failure. FPGA
recovery is recorded separately below.

USB JTAG initially reported USERCODE 0x0573, whose image remains unidentified.
The carrier development chat supplied the known MeOS-compatible filtered-reply
image (SHA256 `85bac22420a81bbaaef26a2edff81a3deb0e2bb114a97f106c1deede8062d717`).
With P4 held in ROM, SRAM programming and independent USER/STATUS reads verified
0xE368/0x6020. After P4 reboot, 1MHz ID succeeded. Flash was not written.
See the [USB recovery log](validation/fractal-studio-usb-recovery-2026-11-04.json).

The [complete retest](validation/fractal-studio-recovered-hardware-2026-11-04.json)
passed CPU/GPU modes, animation pause/resume, all four resolutions, exact
1024/4096/16384/65536-pixel Julia comparisons, GPU continuous playback and
switching back to CPU during GPU playback. Internal/PSRAM used bytes returned
to baseline on exit, and display errors/timeouts were zero, with submitted and
retired batches equal. Active-frame status can include shell console painting;
this is not an independent LCD visual assessment or sustained-load qualification.
The [scene retest](validation/fractal-studio-scenes-hardware-2026-11-04.json)
also checks Mandelbrot and Julia with exact 128x128 comparisons.

## Previous revision and hardware records

## Current revision and stop status — 2026-11-04

The SD application now starts CPU Julia animation at a true128x128 resolution.
R cycles32/64/128/256; Space or A pauses/resumes. C/G selects CPU/FPGA,
M/J selects the scene, V compares the complete frame, Q exits. Julia constants
and the Mandelbrot viewport change with elapsed active time. FPS is measured,
not a refresh-rate guarantee. A frame's time/parameters are frozen for all tiles
and both engines. Cancelled partial frames are not presented.

Higher resolutions combine actual32x32 tiles, not merely enlargement. Display
remains up to512x512. The maximum caller workspace is280616 bytes (about274KiB),
which goes to PSRAM because it exceeds the192KiB internal allocator pool. The
kernel communication workspaces remain separate. High-resolution GPU calls
are slower: the observed256x256 comparison used64 device calls and about7045ms
of GPU-call time, versus118ms CPU calculation; LCD submission was154ms.

Latest file:4612 bytes, image4548/code3604, stack4096, SHA256
`833c64b5c6dfbf547aa2d1a174c399ad3bbfbc11b6c2ab82dc2a1f3ab337bfd2`.
It is installed at `/sd/fractal.m5ex`; the OS firmware was not rewritten for
these application changes.16 host scenarios passed, including independent
global-pixel/tile mapping, pause/time behavior and cancellation before display.

The [32x32 animation run](validation/fractal-animation-hardware-2026-11-04.json)
passed CPU/GPU motion, pause/resume and comparisons, with RAM restored after
exit. The [high-resolution run](validation/fractal-highres-hardware-2026-11-04.json)
confirmed default128 animation, pause, resolution changes, and complete matching
frames at32/64/128/256 (1024/4096/16384/65536 pixels). That run then **failed to
obtain the expected GPU-only128 mode**: the next report remained CPU mode.
The cause is unresolved; GPU-only high-resolution continuous playback and final
high-resolution exit/memory checks are not qualified by that run. Development
stopped at the user's request, without further changes or experiments.

Input polling uses a10ms UART timeout. Infinite blocking prevented animation;
a shared zero timeout starved the lower-priority app. Both failed attempts are
retained as historical evidence. The focus lease restores the previous timeout.

日本語：自動アニメーション、標準128x128画素、Rで32/64/128/256切り替え、
Space/Aで一時停止・再開に対応しました。全解像度でCPU/FPGAの全画素一致を
確認しましたが、高解像度GPU単独モードの切り替え確認は未解決です。
ユーザーの停止指示で開発を止め、現在の状態を保存しています。

## Historical static revision (the following scope is superseded above)


`apps/m5ex/fractal.c` builds into a separately loaded RISC-V M5EX application for Tab5. It is not an embedded firmware image or a shell built-in. The application uses the existing M5EX API table and the `/dev/display`, `/dev/uart` and `/dev/fpga` device interfaces.

## Rendering scope

The application calculates one **32 × 32 RGB565 tile**, using the carrier's canonical Q4.12 fixed-point reference and the same 64-iteration Mandelbrot/Julia parameters on CPU and FPGA. The image is enlarged with nearest-neighbor replication, up to scale 16 (512 × 512 display pixels). This does **not** calculate a 512 × 512 fractal. Display size limits can reduce the scale.

CPU mode calls `fractal_reference_tile` in the loaded application. FPGA mode submits parameters through `FPGA_IOC_FRACTAL_RENDER` and displays actual validated FPGA readback. A failed GPU request or a failed comparison does not display a replacement CPU image. Selecting CPU mode explicitly remains possible after an error.

## Build and run

Use Python with `pyelftools` and the ESP32-P4 RISC-V toolchain. From the repository root:

```sh
python tools/m5exbuild.py --arch riscv --cc /path/to/riscv32-esp-elf-gcc apps/m5ex/fractal.c -o fractal.m5ex
```

The build tool selects the RISC-V linker script and Tab5 definitions; it does not modify the firmware's embedded application table. Do not use `--embed` for this application.

The target firmware must explicitly enable trusted native execution with `-DM5OS_NATIVE_M5EX=ON`. Build with a **fresh sdkconfig path** so `CONFIG_ESP_SYSTEM_PMP_IDRAM_SPLIT` is disabled by the native defaults; the option does not override an already saved conflicting setting. See [native execution policy and build instructions](NATIVE_M5EX.md). A normal split-protected P4 build refuses native loading rather than executing heap memory. This is a firmware build requirement, separate from compiling the application.

Copy the resulting file to the mounted microSD as `/sd/fractal.m5ex`. The existing serial upload helper can also place it there (replace `SERIAL_PORT` with the connected Tab5 port):

```sh
python tools/m5ex_push.py SERIAL_PORT fractal.m5ex /sd/fractal.m5ex
```

Run in the MeOS shell:

```text
run /sd/fractal.m5ex
```

CPU mode starts first. FPGA mode requires firmware containing the new device implementation, the configured physical parallel connection, and a compatible triangle/fractal FPGA image. Loading the application alone does not add those firmware or FPGA capabilities.

| Key | Operation |
| --- | --- |
| C | Select CPU calculation; disable comparison mode. |
| G | Select FPGA calculation; disable comparison mode. |
| M | Select Mandelbrot. |
| J | Select Julia. |
| V | Calculate both engines and compare all 1024 RGB565 pixels; preserve the selected display engine. |
| Q, Escape or Ctrl-C | Exit and release device leases/resources. |

A comparison stops at the first differing pixel and reports its coordinates and CPU/FPGA values. `compare PASS1024` means all 1024 pixels matched for that frame; it is not a performance or sustained-operation qualification.

## Timings and diagnostic output

The screen distinguishes these quantities:

- **CPU milliseconds:** application-side fixed-point calculation, measured with `uptime_ms`. A measured zero is displayed as `<1 ms`; an engine not executed is shown as `-`.
- **FPGA call milliseconds:** synchronous device-call wall time. Device `elapsed_us` is labeled **transport+compute** and includes communication and device work; it is not pure accelerator compute time.
- **Engine cycles:** optional FPGA-reported cycle counter, accompanied by a validity indicator. No conversion to microseconds or speedup claim is made without a verified engine clock and equivalent measurement boundaries.
- **Draw milliseconds:** LCD submission path, separate from fractal calculation. The UART JSON measurement includes frame publication; the on-screen draw value is sampled before the final status text/frame publication completes.

Each render attempt emits a bounded JSON line on UART after ending the display frame. Fields include `engine`, `scene`, `width`, `height`, `compare`, `mismatch`, `error`, measured flags, CPU/FPGA milliseconds, `gpu_transport_compute_us`, and `draw_ms`. `mismatch=-1` means no differing pixel was recorded; check `compare` and `error` before interpreting it as a successful comparison. `error=0` is the application/device success result. Millisecond zero in JSON retains the raw timer value, rather than the screen's `<1` formatting.

## Device and resource boundaries

The manifest requests console and display permissions (`0x03`) and a 4096-byte task stack. `/dev/fpga` exposes a fixed graphics ioctl under display permission, not arbitrary GPIO access. Its versioned, sized request supplies the canonical parameters and a 1024-word output buffer; output is valid only after successful validated readback and physical-session cleanup.

The application acquires `UART_IOC_FOCUS` before reading keys. It does not steal focus from a live owner. It also acquires `DISPLAY_IOC_CONSOLE_PAUSE` to keep console repainting from covering its image. CPU/FPGA work runs before `DISPLAY_IOC_BEGIN_FRAME`; the display frame is held only during clearing, image blits and status text, then released with `DISPLAY_IOC_END_FRAME`. A busy frame does not trigger unguarded drawing. Cleanup releases the frame, console pause and input focus before closing their devices.

Two 1024-word tiles and a single expanded source-row strip live in an application-owned heap allocation (about 20 KiB), not on the task stack. The strip is replicated into 32 display blits per image; no full 512 × 512 application framebuffer is allocated. The OS display buffers and device-side communication buffers are additional resources.

## Validation record

The final standalone artifact built with the ESP32-P4 GCC 14.2 toolchain and warnings treated as errors:

| Item | Value |
| --- | --- |
| Local artifact | `D:/tmp/meos-idf/fractal-riscv.m5ex` |
| File size | 3264 bytes |
| Loaded image | 3200 bytes, including 2400 bytes of code |
| Relocations | 0 |
| Task stack | 4096 bytes |
| SHA-256 | `ef95077f98b1f3f164d4e30fb4ac5966891125968341ff98abf40e0ed4b6035f` |

`tests/test_fractal_app.py` compiles the actual application against host device/API models. Ten scenarios cover controls, pixel comparison, GPU failure without fallback, allocation/display failures, busy/end-failing frame handling, input-focus failure and refusal to steal foreign focus. The model checks balanced allocations/device closes, frame/pause/focus cleanup, no GPU call under a display frame, and bounded per-render JSON. These tests simulate the GPU and display; they do not establish physical rendering, timing accuracy or image quality.

### Hardware application check — 2026-11-04

The [recorded hardware run](validation/fractal-app-hardware-2026-11-04.json) passed using the 3264-byte application hash above. The firmware was 1189568 bytes, SHA-256 `6c41bd04b8d4196d4b054190d5a0be8fc31458cc0428ec34a6d3b46ea9d99425`, with native M5EX enabled and PMP IDRAM split disabled. A separately loaded `m5hello` first confirmed native execution. The fractal application was then uploaded to persistent SD storage at `/sd/fractal.m5ex` and launched from that path.

The run exercised CPU/FPGA switching, both scenes, comparisons of both complete 1024-pixel tiles, and exit back to the shell. Both comparisons reported `error=0`, `compare=true` and `mismatch=-1`. Display status after exit reported 35 submitted/retired batches, zero errors/timeouts and no active frame; these counters include the session's display activity. The serial record establishes submission/comparison and cleanup, not an independent visual-quality assessment.

| Observed operation | CPU calculation | FPGA call including transport | LCD submission |
| --- | --- | --- | --- |
| Mandelbrot | 2 ms | about 111 ms (110589–110944 µs device time) | 148–160 ms across the recorded modes |
| Julia | 7 ms | about 111 ms (111351–111397 µs device time) | 142–151 ms across the recorded modes |

These small-tile results show that the current FPGA device-call path is slower end to end than local CPU calculation. They do not establish an accelerator compute speedup; setup, transfer and display costs remain separate optimization targets. The application check does not qualify external 20 MHz communication or a larger GUI.

Internal heap use returned from 94416 bytes before the run to 94416 bytes after exit, with recorded peak 147120 bytes. PSRAM use returned to 8588864 bytes, with peak 8594672 bytes. These are session allocator measurements including loaded-code/task/device activity, not an isolated application workspace measurement or proof of zero runtime allocation. No allocation failures were recorded.

## 日本語での使用方法

これはファームウェアへ組み込む機能ではなく、SDから起動する独立したM5EXアプリです。ビルドしたファイルを `/sd/fractal.m5ex` に置き、`run /sd/fractal.m5ex` で起動します。CでCPU、GでFPGA、MでMandelbrot、JでJulia、Vで両者の全1024画素比較、Qで終了します。

計算する画像は32×32画素です。最大512×512への拡大表示であり、512×512画素を計算しているわけではありません。CPU計算時間、FPGA呼び出し時間（通信を含む）、LCD描画時間を分けて表示します。FPGAエラー時にCPU画像へ自動で置き換えることはありません。2026年11月4日の実機試験で、SDからの起動、CPU／FPGA切り替え、両画像の全1024画素一致、終了後のメモリ回収を確認しました。現在の32×32画像ではFPGA呼び出しは通信込みで約111ms、CPU計算は2～7msで、FPGAによる高速化を示す結果ではありません。実機ログと測定範囲は上記を参照してください。


For the already-reviewed Dock wiring, the supporting native firmware also needs
`-DM5OS_FPGA_DOCK_VERIFIED=ON`. This gate remains separate from
`-DM5OS_NATIVE_M5EX=ON`; enabling native code alone does not authorize physical
GPU commands. The installed diagnostic build contains both options, while the
three standard published firmware sizes retain their normal defaults.
