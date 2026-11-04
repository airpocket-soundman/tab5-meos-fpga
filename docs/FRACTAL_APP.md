# Standalone CPU/FPGA fractal application

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
