# Feature modules

A build contains the core system plus the optional modules it asks for. Each module declares what it needs, and the
build adds those with it, so a selection is always complete. The registry is `cmake/modules.cmake`.

## Choosing modules

```text
idf.py -DM5OS_MODULES="mp_camera,uvc"       exactly these, plus what they need (here also micropython and camsrc)
idf.py -DM5OS_MODULES=all                   every module this board can have
idf.py -DM5OS_DISABLE_MODULES="mjpeg,sl"    the defaults without these (and without whatever needs them)
(nothing given)                             the defaults; the older switches M5OS_MICROPYTHON, M5OS_BLUETOOTH and
                                            M5OS_SL still work
```

`python tools/m5os_modules.py list` shows the modules; `resolve a,b` prints what a selection would contain and why;
`cmake a,b` prints the `idf.py` argument; `json` is the machine-readable registry (for a flasher page or a CI matrix).
The configure step prints the same table, for example `[on ] uvc  (needed by mjpeg)`.

| module | what it is | needs |
| --- | --- | --- |
| `camera` | built-in SC202CS camera: CSI/ISP driver, `camera` app, `snap` | - |
| `uvc` | USB (UVC) camera, raw YUY2 frames, `uvc` command | the native-DMA USB backend |
| `mjpeg` | Motion-JPEG from USB cameras: software JPEG decoder with reduced-size decoding | `uvc` |
| `camsrc` | one interface over the built-in and USB cameras (`uvc live`) | `camera` or `uvc` |
| `micropython` | MicroPython interpreter and `machine` bindings | - |
| `mp_camera` | MicroPython `camera` module | `micropython`, `camsrc` |
| `bluetooth` | BLE HID host (experimental, off by default) | - |
| `sl` | SL animation | - |
| `fpga` | Dock jumper LINK v0: native GPIO, ID/echo/invert and 513-exchange diagnostic (off by default) | Tab5 |
| `fpga_gfx` | Experimental Dock GFX v0: 32x32 RGB565, drawing/readback, full-image test and LCD demo (off by default) | `fpga` |
| `gfxbench` | Native GPU LAB CPU program: eight 640x360 scenes, polygon/Z, float/integer raster, benchmarks (off by default) | Tab5, PSRAM |
| `fpga_npu` | NPU CPU conformance: MAC/requant/dot/GEMM/conv; external NPU protocol pending (off by default) | `fpga` |
| `fpga_diag` | Jumper diagnostic bundle, feature inventory and coverage reporting (off by default) | `fpga_gfx`, `fpga_npu`, `gfxbench` |

Storage and cameras are also named devices (`sd0`, `usb0`.., `camera0`, `camera1`): see [DEVICES.md](DEVICES.md).

Everything not listed (kernel, shell, display, touch, keyboards, USB host and storage, Ethernet, Wi-Fi, SSH/SFTP,
Tailscale, IO services, image viewer, ...) is core for now and is built into every image.

FPGA bringup is selected explicitly with `-DM5OS_MODULES=fpga` (or added to an
existing comma-separated selection). It is not included in the named size
profiles. See [FPGA_LINK.md](FPGA_LINK.md) for wiring prerequisites and commands.
`-DM5OS_MODULES=fpga_gfx` includes GFX and its `fpga` dependency. It uses the
carrier's hash-pinned development specification; a matching GFX bitstream is
required, and hardware communication/display remain unverified.
`-DM5OS_MODULES=gfxbench` enables the independent CPU graphics test program,
without an FPGA dependency. See [GFXBENCH.md](GFXBENCH.md) for scenes, controls
and measurements. It can coexist with `fpga_gfx` in the same selection.
`-DM5OS_MODULES=fpga_diag` bundles all current FPGA diagnostics and the CPU
graphics program. NPU conformance is explicitly CPU-only until the carrier
provides its external NPU commands and bitstream. See [FPGA_NPU.md](FPGA_NPU.md)
for per-function tests and how incomplete hardware coverage is reported.

## Firmware sizes

Three named selections are published on the project site (`idf.py -DM5OS_PROFILE=small|medium|large`, defined in
`cmake/modules.cmake`). Measured with ESP-IDF v5.5.2, application image (`m5os.bin`):

| size | application | modules | boot-checked on the Tab5 |
| --- | --- | --- | --- |
| small | 1,229,120 B | none: shell, storage, USB host, network, SSH | yes: no camera, no `python` |
| medium | 1,446,384 B | `camera`, `camsrc`, `micropython`, `mp_camera`, `sl` | yes: `camera0`, MicroPython capture, no USB camera |
| large | 1,462,288 B | medium plus `uvc` and `mjpeg` | yes: boots with `uvc` and `camera`; no USB camera was attached for this check |

`bluetooth` is in none of them: with it the image does not link, because internal RAM overflows. Whether that is caused by
the changes of the last days or was already so has not been separated. A Bluetooth build is a custom one
(`-DM5OS_MODULES=...,bluetooth` and trimming something else).

## The site

`.github/workflows/pages.yml` builds the three sizes in the ESP-IDF container (`espressif/idf:v5.5.2`, not yet run on
GitHub), then publishes the site: the roadmap, the command reference (`tools/build_reference.py`, from the shell command table
in `user/shell/shell.c` and `docs/MICROPYTHON_API.md`), and the firmware page (`docs/site/flash/`). The images and
`manifest.json` (offsets from each build's `flash_args`, sizes, SHA-256, the modules of each size, from
`tools/make_firmware_manifest.py`) are placed under `firmware/` and are not committed.

The page connects with the browser's Web Serial (Chrome or Edge), reads the chip back through `esptool-js` 0.5.7 and writes
the bootloader (0x2000), partition table (0x8000) and application (0x10000) without erasing the whole flash or touching the
ESP32-C6. Checked in the browser pane of this session: the page renders with the real manifest, `esptool-js` loads, and the
error is shown when no port is chosen. **Not checked:** the port chooser, the connection to a Tab5 and the write itself
(the pane has no port chooser). The same images were written with `esptool` and booted.

## How a module is left out

Every module becomes a compile definition `M5OS_MODULE_<NAME>=0|1` on all sources. Code guards itself with
`#if M5OS_MODULE_<NAME>`:

- `camera` off: the built-in camera driver is replaced by `drivers/camera/camera_stub.c` (reports "no camera").
- `uvc` off: `native_uvc.c` keeps only stubs, so USB enumeration still works but no device becomes a camera.
- `mjpeg` off: `drivers/mjpeg/mjpeg.c` compiles to nothing and MJPEG modes cannot be selected.
- `camsrc` / `mp_camera` off: the files compile to nothing (or stubs); `uvc live` says it needs `camsrc`.
- `micropython`, `bluetooth`, `sl` keep their own CMake files; the selection decides their option.

A build with only `mjpeg` (which pulls in `uvc`) was built and run on the Tab5: the image is 1,241,552 bytes against
1,459,488 for the default one, `uvc modes`/`uvc size ... mjpeg` work, `camera` has nothing behind it and there is no
`python` command.

## Adding a module

1. `m5os_module(<name> DESCRIPTION "..." [BOARDS ...] [DEFAULT ON|OFF] [OPTION <old switch>] [DEPENDS ...]
   [DEPENDS_ANY ...])` in `cmake/modules.cmake`.
2. Guard the sources with `#if M5OS_MODULE_<NAME>` (give the symbols other code calls a stub in the `#else`).
3. Add it to the table above. `tools/m5os_modules.py` and the configure output pick it up from the registry.

## Not done yet

The older features are not modules yet: separating them needs stubs for every symbol the shell and other code call.
The registry and the guards are the pattern to follow for SSH/SFTP, Tailscale, USB Ethernet, USB storage, BLE and the
FPGA link; the dependencies between them (for example SSH and Tailscale need the network stack, Ethernet needs the USB
host) have to be written down as each one is separated.
