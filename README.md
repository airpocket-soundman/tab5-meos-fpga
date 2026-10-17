# M5OS

An educational and experimental lightweight operating system built from scratch for M5Stack devices (**M5Stack CoreS3**, **M5Cardputer**, and **M5Stack Tab5**).
M5OS implements its own **preemptive/cooperative scheduler, task management, timer, IPC message queues, memory allocator, VFS, custom filesystem (M5FS), dynamic binary loader (M5EX), GUI/TUI window system, camera driver, and SDIO Wi-Fi stack**—without ever starting FreeRTOS.

---

## Progress

| Phase | Feature | Status |
|---|---|---|
| 1 | Bare-metal boot, UART (USB-Serial-JTAG) output | ✅ Verified on hardware |
| 2 | Cooperative scheduler (`task_create/yield/sleep/exit`) | ✅ Verified on hardware (`ABAB…`, 100 ms sleep granularity) |
| 3 | Preemptive scheduler (CCOMPARE1 / CLIC tick ISR, custom vector table) | ✅ Verified on hardware (two CPU hogs share ~1:1; prio-3 task wakes within 210 ms under load) |
| 1.5 | LCD driver (`display_*` API) | ✅ Verified on hardware |
| 4 | Memory: boundary-tag free-list heap, separate internal SRAM / PSRAM arenas, `kmalloc*` | ✅ Verified on hardware (`selftest ALL PASS`) |
| 5 | Device abstraction (`device_open/read/write/ioctl`), blocking mutex, `uart`/`display`/`buttons` | ✅ Verified on hardware |
| 6 | M5FS custom filesystem + SD block device | ✅ M5FS verified on hardware over RAM disk |
| 7 | VFS (`/dev` = devfs, `/` = M5FS), mount table | ✅ Verified on hardware (`VFSTEST ALL PASS`) |
| 8 | Interactive Shell (`help ps kill ls cat echo mem uptime run` + file & network utilities) | ✅ Verified on hardware |
| 9 | App lifecycle (`app_start/stop/list`, permissions, automatic resource reclamation) | ✅ Verified on hardware (`APPTEST ALL PASS`) |
| 10 | App loader (`M5EX` relocatable binary format, dynamic loading over serial/FS) | ✅ Verified on hardware (`M5EXTEST ALL PASS` + host push & run) |
| 11 | IPC (fixed-size per-task mailboxes, blocking/timeout send & receive) | ✅ Verified on hardware (`MSGTEST ALL PASS`) |
| 12 | Touch GUI (window manager, launcher, Terminal / Clock / Calculator / Snake) | ✅ Verified on hardware (`GUITEST ALL PASS`) |
| 13 | TUI Window System (`dqwin`), Full-Screen Filer (`files`), and Text Editor (`edit`) | ✅ Verified on M5Cardputer & M5Stack Tab5 |
| 14 | Camera App (`camera` / `snap`, MIPI-CSI SC202CS + ISP) & Filer BMP Image Viewer | ✅ Verified on M5Stack Tab5 |
| 15 | Graphical Wi-Fi Manager (`wifi`) & FreeRTOS-free ESP32-C6 SDIO / ESP-Hosted Stack | ✅ Verified on M5Stack Tab5 |

---

## Supported Boards

| Feature | M5Stack CoreS3 | M5Cardputer (Original / ADV) | M5Stack Tab5 |
|---|---|---|---|
| **SoC** | ESP32-S3 (Xtensa LX7) + 8 MB PSRAM, 16 MB Flash | ESP32-S3FN8: 8 MB Flash, **no PSRAM** | **ESP32-P4 (Dual-core RISC-V @ 400 MHz)** + 32 MB PSRAM, 16 MB Flash + **ESP32-C6** co-processor |
| **Display** | ILI9342C 320×240 (SPI, via AXP2101/AW9523) | ST7789 135×240 in landscape (240×135) | ST7121 / ILI9881C 720×1280 MIPI-DSI in landscape (**1280×720**) |
| **Input** | Capacitive touch (FT6336U) | **Built-in Keyboard** (74HC138 matrix / TCA8418 I2C) | **Tab5 Keyboard** (I2C HID `0x6D`) or USB-A HID boot keyboard |
| **UI Mode** | Touch GUI (Window Manager + Launcher) | Full-screen console (`fbcon`) + TUI apps (`dqwin`) | High-res console (`fbcon` 80×22) + TUI apps (`files`, `edit`, `camera`, `wifi`) |
| **Camera** | — | — | **SC202CS** 1600×1200 MIPI-CSI + Hardware ISP + Auto-Gain/White-Balance |
| **Wi-Fi** | — | — | **ESP32-C6** over SDIO Slot 1 (ESP-Hosted RPC + DHCPv4/ARP/ICMP) |
| **Build Command** | `tools/build.sh cores3` | `tools/build.sh cardputer` | `tools/build.sh tab5` |

```sh
# Build and flash for M5Stack Tab5 (ESP32-P4)
tools/build.sh tab5 build
tools/build.sh tab5 flash -p /dev/cu.usbmodemXXXX

# Build and flash for M5Cardputer
tools/build.sh cardputer build
tools/build.sh cardputer flash -p /dev/cu.usbmodemXXXX

# Build and flash for M5Stack CoreS3
tools/build.sh cores3 build
tools/build.sh cores3 flash -p /dev/cu.usbmodemXXXX
```

Board-specific code is cleanly separated into `*_cores3.c`, `*_cardputer.c`, and `*_tab5.c` and selected via CMake (`M5OS_BOARD`).
Board capabilities (name, resolution, touch/keyboard availability) are exposed to the kernel through `kernel/include/m5os/board_config.h`.

---

## Build & Run

```sh
. tools/env.sh                                                   # Activate ESP-IDF virtualenv
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash
python tools/monitor.py /dev/cu.usbmodemXXXX 5                   # View boot logs for 5 seconds
python tools/shell_test.py /dev/cu.usbmodemXXXX 'ps' 'ls /dev'   # Send commands to the shell and print output
screen /dev/cu.usbmodemXXXX 115200                               # Interactive serial session (exit: Ctrl-A K)
M5OS_SELFTEST=1 idf.py build                                     # Run full regression test suite at boot
```

---

## Architecture

```
 Power On
   │
   ▼
 ROM ──► Espressif 2nd-stage Bootloader ──► call_start_cpu0()        (IDF: cache / flash / clock / PSRAM)
                                              │
                                              ▼
                                       start_cpu0_default()          (IDF: peripheral & memory init)
                                              │ esp_startup_start_app()
                                              │   ▲ Normally calls vTaskStartScheduler()
                                              ▼   │ Intercepted via linker flag -Wl,--wrap
                                     __wrap_esp_startup_start_app()  kernel/boot/boot_hook.c
                                              │
                                              ▼
 ┌──────────────────────────────────── M5OS ────────────────────────────────────┐
 │ user/    shell  launcher  filer  edit  camera  wifi   apps/  hello clock ... │
 │ ──────────────────────────── Syscall-level APIs ──────────────────────────── │
 │ kernel/  task ─ scheduler ─ timer ─ memory ─ ipc ─ irq                       │
 │ fs/      vfs ── m5fs          drivers/  uart lcd kbd cam wifi sd ...         │
 │ ────────────────────── arch.h / hal_*.h (Porting Boundary) ───────────────── │
 │ hal/esp32s3/ (Xtensa LX7)     hal/esp32p4/ (RISC-V 32-bit CLIC)              │
 └──────────────────────────────────────────────────────────────────────────────┘
```

`kernel/` only depends on the abstract declarations in `kernel/include/m5os/arch.h` and `hal_*.h`. Porting M5OS to another SoC architecture only requires implementing `hal/<chip>/`.

---

## Rules for Running Without FreeRTOS

* `libfreertos.a` is linked because ESP-IDF startup components reference its symbols, but **the FreeRTOS scheduler is never started** (`vTaskStartScheduler` is stripped by `--gc-sections`).
* **Allowed from ESP-IDF**: ROM/bootloader/startup, linker scripts, hardware register headers (`soc/*_reg.h`, `soc/*_struct.h`), low-level inline HAL headers (`hal/*_ll.h`), cache sync (`esp_cache_msync`), and `esp_rom_*`.
* **Not used from ESP-IDF**: FreeRTOS tasks/queues/semaphores, `esp_timer`, `esp_ipc`, newlib `printf`/locks (which internally take FreeRTOS mutexes), or ESP-IDF VFS. All kernel/user formatting goes through our own lock-safe `kprintf` / `ksnprintf`.

---

## Kernel Subsystems

### 1. Cooperative & Preemptive Scheduler (Phases 2 & 3)
* **Xtensa LX7 (`hal/esp32s3/`)**:
  * Uses `window_spill4` to spill register windows to the stack and stores only `sp` in the TCB.
  * Points `VECBASE` to a custom 1 KiB vector table (`hal/esp32s3/vectors.S`) that handles Level-3 timer interrupts (`CCOMPARE1`, 10 ms tick) directly without touching FreeRTOS's `_frxt_int_enter/exit`.
  * Preserves the 16-byte Xtensa ABI base save area at the top of the interrupt frame and spills windows *before* decrementing `sp`.
* **RISC-V (`hal/esp32p4/`)**:
  * Uses the CLIC interrupt controller with a static executable vector table (`hal/esp32p4/vectors_p4.S`).
  * Ensures `mcause` bit 31 (`interrupt`) is set when restoring via `mret` so `mintstatus.mil` is properly restored across context switches.
* **Per-priority Round-Robin**: Maintains an independent round-robin cursor per priority level so frequent high-priority tasks (such as `touchd` or `kbdd`) never starve lower-slot tasks at the same priority.

### 2. Memory Management (Phase 4)
* **Dual Heap Arenas (`kernel/memory/heap.c`)**:
  * **Internal SRAM**: 192 KB static `.bss` arena (`kmalloc_internal` / `kmalloc_dma`) used for task stacks, DMA descriptors, and ISR-accessible structures.
  * **PSRAM**: Up to 8 MB (CoreS3) or 28 MB (Tab5) external SPIRAM arena (`kmalloc_psram`) for large buffers, RAM disks, camera frames, and images.
* **Allocator**: Boundary-tag free-list with 16-byte aligned headers (`magic`, `size`, `prev_size`), immediate left/right coalescing on `kfree()`, and double-free / corruption detection.

### 3. Device Layer & Mutexes (Phase 5)
* Applications access hardware through `device_open()`, `device_read()`, `device_write()`, and `device_ioctl()`—serialized per device via blocking kernel `mutex_t` (`kernel/ipc/mutex.c`).
* When a task is killed via `kill PID` or `app_stop()`, any mutexes held by that task (`task_t.held`) are automatically released so system services (like `kprintf`) never deadlock.

### 4. M5FS Filesystem & VFS (Phases 6 & 7)
* **M5FS (`fs/m5fs/m5fs.c`)**: Custom little-endian block filesystem (512-byte blocks):
  * Block 0: Superblock (`M5FS` magic, version, geometry, free block count, CRC32).
  * Bitmap blocks + 16 directory blocks (128 entries × 64 bytes) + data blocks.
  * Each file stores up to **4 extents** (`{start, count}`) of contiguous blocks, avoiding FAT chains.
* **VFS (`fs/vfs/vfs.c`)**: Longest-prefix mount table mounting `devfs` at `/dev` and `M5FS` at `/` (backed by SD card if a valid M5FS volume is present, or an automatic PSRAM/SRAM RAM disk with `/system`, `/apps`, and `/home`).

### 5. App Lifecycle, Permissions & M5EX Dynamic Loader (Phases 9 & 10)
* **Lifecycle (`kernel/app/app.c`)**: Separates user apps from kernel tasks. Tracks per-app permissions (`C`=console, `D`=display, `I`=input, `r`/`w`=file read/write, `s`=task spawn, `m`=IPC), open VFS handles, child tasks, and `umalloc()` memory blocks. When an app exits or is killed, the `appd` kernel daemon reclaims all owned tasks, files, windows, and heap allocations.
* **M5EX Loader (`kernel/loader/m5ex.c`)**: Relocatable binary format (`header(64B) | image | relocation_table`) with CRC32 validation and a capability-checked kernel function table (`m5_api_t`). New apps can be compiled on the host (`tools/m5exbuild.py`) and pushed live over USB serial (`tools/m5ex_push.py`).

### 6. IPC Message Queues & Touch GUI (Phases 11 & 12)
* **IPC (`kernel/ipc/msg.c`)**: Per-task 8-slot FIFO mailboxes of 32-byte messages (`sender`, `type`, `data[24]`) with kernel-stamped sender PIDs and blocking/timeout send & receive.
* **GUI (`kernel/gui/gui.c`)**: Full-screen stacked window manager for touch devices (CoreS3) with IPC event delivery (`DOWN`/`MOVE`/`UP`/`EXPOSE`/`CLOSE`), system bar (`Home` / `Next` / `Close`), Launcher, Terminal, Clock, Calculator, and Snake.

---

## M5Stack Tab5 Port (ESP32-P4, RISC-V)

Build: `tools/build.sh tab5 build` / Flash: `tools/build.sh tab5 flash -p <port>` (uses ESP-IDF 5.4.4).

Key hardware subsystems supported on M5Stack Tab5:
* **Display (`drivers/lcd/display_tab5.c`)**: 720×1280 MIPI-DSI panel (ST7121 / ILI9881C) driven in **1280×720 landscape** (180° rotated in software `put()` with `esp_cache_msync` writeback).
* **I/O Expanders (`hal/esp32p4/board_tab5.c`)**: Dual PI4IOE5V6408 I2C expanders (`0x43` / `0x44`) controlling LCD/touch reset, speaker enable, camera reset, USB 5V host power, and ESP32-C6 Wi-Fi power.
* **Keyboard (`drivers/tab5kbd`, `drivers/usbkbd`)**: Polls the official M5Stack Tab5 I2C Keyboard (`0x6D` on Ext.Port1 G0/G1) in HID mode, with automatic fallback to USB-A HID boot keyboards over the DWC2 host controller.

---

## TUI Window System (`dqwin`), Filer (`files`), and Text Editor (`edit`)

Keyboard-equipped boards (**M5Cardputer** and **M5Stack Tab5**) feature a Dragon Quest-inspired retro TUI window toolkit (`user/tui/dqwin.c`) with thick white rounded borders on a black background and a `▶` selection cursor. Layout uses character cells (`8×8` glyph × `M5OS_FBCON_SCALE`), scaling cleanly from 40 columns on Cardputer to 80×22 on Tab5, and works simultaneously on both the built-in LCD+keyboard and serial terminals.

### Full-Screen Filer (`files [DIR]`)
Implemented in `user/filer/filer.c` (inspired by VZ Editor's filer). Run `files` from the shell or launch `run files`.

| Key | Action |
|---|---|
| `↑` `↓` `←` `→` `PgUp` `PgDn` `Home` `End` | Navigate directory entries (`←`/`→` moves across columns) |
| `Enter` / `BS` | Enter directory, view `.bmp` image, or view text file / Go to parent directory |
| `Space` | Toggle file selection mark (`*`) |
| `C` / `M` | Copy / Move marked files (or file under cursor) |
| `D` | Delete file or empty directory (with confirmation dialog) |
| `K` | Create directory (`mkdir`) |
| `N` | Rename file |
| `V` | Text/Hex viewer (first 8 KB) |
| `P` | **Image Viewer** (renders 24-bit `.bmp` photos full-screen) |
| `A` | Launch **Camera App** directly from Filer and refresh directory on exit |
| `W` | Launch **Graphical Wi-Fi Settings App** directly from Filer |
| `E` / `F` | Open file in Text Editor / Create new file in Text Editor |
| `Q` / `ESC` | Exit Filer |

### Text Editor (`edit FILE`)
Implemented in `user/edit/edit.c`. Edits ASCII text files up to 32 KB with `Ctrl-S` to save, `Ctrl-Q` / `ESC` to quit (with unsaved changes prompt), arrow keys, `Home`/`End`, `PgUp`/`PgDn`, and 2-space Tab insertion.

---

## Camera App (`camera` / `snap`) & BMP Image Viewer

Implemented in `drivers/camera/camera_tab5.c` and `user/camera/camera_app.c` for M5Stack Tab5:
* **Hardware Pipeline**: Drives the onboard **SmartSens SC202CS** (1600×1200 RAW8 BGGR sensor at I2C `0x36`) over **MIPI-CSI** + **Hardware ISP** (configured with `ISP_COLOR_RAW8` → `ISP_COLOR_RGB565`) + **DW-GDMA** into PSRAM at 800×600 resolution, using a 24 MHz external clock routed out of `GPIO36` via LEDC.
* **Image Quality & Color Calibration**:
  * Configures SC202CS manual exposure + analog/digital coarse/fine gain and applies per-channel RGB white-balance gains during downscaling.
  * Properly handles ESP32-P4 ISP RGB565 byte order in both live preview and 24-bit `.bmp` encoding so reds, greens, and blues are natural and accurate.
* **Interactive Camera App (`camera` or `run camera`)**:
  * Live 640×480 preview centered on the 1280×720 display.
  * Press `Space` or `Enter` to capture and save a 320×240 24-bit uncompressed `.bmp` photo (`/home/CAP_0001.BMP`, `CAP_0002.BMP`, ...).
  * Press `V` to immediately open the last captured photo in the BMP viewer, or `Q`/`ESC` to return.
* **Headless CLI Snapshot (`snap [FILE]`)**: Captures a photo directly from the shell command line (after 5 warmup frames) and saves it to `/home/CAP_XXXX.BMP` or the specified path.
* **Filer BMP Viewer (`user/filer/bmpview.c`)**: Selecting any `.bmp` file in `files` and pressing `Enter` or `P` renders the image scaled up to 3× (960×720) with metadata overlay.

---

## Graphical Wi-Fi Manager (`wifi`) & ESP32-C6 SDIO Stack

Implemented in `drivers/wifi/wifi_tab5.c` and `user/wifi/wifi_app.c` for M5Stack Tab5:

### FreeRTOS-Free ESP32-C6 SDIO & ESP-Hosted Driver (`drivers/wifi/wifi_tab5.c`)
* **Polled SDMMC Slot 1 Driver**: Drives ESP32-P4's SDMMC Host Slot 1 (`CLK=GPIO12`, `CMD=GPIO13`, `D0..D3=GPIO11..8`, `C6_RST=GPIO15`) in 4-bit 20 MHz SDIO mode using polled IDMAC descriptors and explicit L1/L2 cache synchronization (`esp_cache_msync`)—with zero FreeRTOS dependencies.
* **ESP-Hosted-MCU Protocol**: Implements the 12-byte `esp_payload_header` streaming transport, slave token flow control, `ESP_PRIV_IF` capability handshake, and a lightweight Protobuf wire-format encoder/decoder over `ESP_SERIAL_IF` (`RPCRsp` / `RPCEvt`).
* **Built-in DHCPv4, ARP & ICMP Stack (`ESP_STA_IF`)**: Handles Ethernet II frames directly over SDIO Function 1, automatically acquiring an IPv4 address, netmask, and gateway via DHCP (`Discover` → `Offer` → `Request` → `Ack`) and responding to ARP requests and ICMP Echo (`ping`) packets on the local network.
* **Persistent Credentials & Auto-Connect (`wifid`)**:
  * Saves credentials both to `/home/wifi.cfg` (`ssid=...`, `pass=...`, `auto=1`) and to the **ESP32-C6's own NVS flash** (`RPC_REQ_WIFI_SET_STORAGE = FLASH`), so remembered Wi-Fi networks survive power cycles even when running on a volatile RAM disk.
  * The background `wifid` kernel task automatically connects to the saved network at boot and reconnects if the link drops (unless manually disconnected by the user).

### Graphical Wi-Fi App (`wifi` / `run wifi` / `W` in Filer)
* **Top Status Window**: Displays connection state (`[CONNECTED]` / `[DISCONNECTED]`), current SSID, channel, RSSI dBm, IPv4 address, gateway, MAC address, remembered SSID, and `[Auto-Connect: ON/OFF]`.
* **Interactive Network List**: Lists scanned 2.4 GHz APs sorted by signal strength with color-coded 4-bar graphical signal meters, security badges (`OPEN`, `WPA2`, `WPA3`, `WPA2/3`), channel numbers, and `[CONNECTED]` / `[SAVED]` badges.
* **Keyboard Controls**:
  * `↑` / `↓` / `PgUp` / `PgDn`: Select a Wi-Fi network from the list
  * `Enter` / `C`: Connect to the selected network (pre-fills saved password if matching, or opens the graphical password input dialog for secured networks)
  * `S`: Rescan nearby Wi-Fi networks
  * `D`: Open the **Graphical Disconnect Dialog** to disconnect from the current AP
  * `A`: Toggle automatic connection at boot (`ON` / `OFF`)
  * `X`: Forget saved Wi-Fi credentials
  * `Q` / `ESC`: Close the Wi-Fi manager

### Command-Line & GUI Wi-Fi Commands in Shell
```sh
wifi                  # Launch the full-screen graphical Wi-Fi configuration app
wifi gui              # Same as above
wifi disconnect gui   # Open the graphical disconnect confirmation dialog from the CLI
wifi disconnect       # Immediately disconnect from Wi-Fi via CLI
wifi status           # Print current Wi-Fi state, SSID, RSSI, IP, gateway, MAC, and saved profile
wifi scan             # Scan and print nearby Wi-Fi networks in a formatted table
wifi connect <SSID> [PASSWORD]  # Connect to an AP from the CLI and save credentials
wifi auto [on|off]    # Enable or disable automatic connection on boot
wifi forget           # Erase saved Wi-Fi credentials from /home/wifi.cfg and ESP32-C6 NVS
```

---

## Known Limitations / TODO

* Single-core execution (`CPU1` is not started).
* Task stacks default to 4 KB (`kmalloc_internal`) without hardware stack guard pages (heavy tasks like `shell`, `wifid`, and `app` use 6–8 KB stacks via `task_create_stack`).
* Do not use floating-point / PIE coprocessors across tasks on ESP32-S3 (lazy FPU context saving is not implemented).
* Strict priority scheduling without mutex priority inheritance.
* Touch input on Tab5 (GT911 / ST7123) and RISC-V M5EX SDK build toolchain are planned for future updates.
