# Tab5 MeOS FPGA

English | [日本語](README.ja.md)

An experimental MeOS fork for a graphical M5Stack Tab5 environment with FPGA-assisted rendering. MeOS handles input, windows and application state; a Tang Primer 20K and its 128 MB DDR3 are intended to hold graphics assets and accelerate selected drawing operations.

**[Development plan & feature status — English / 日本語](https://airpocket-soundman.github.io/tab5-meos-fpga/)**

## Current status

Baseline and size-optimized firmware built and flashed; boot and serial shell verified on Tab5. microSD and two USB drives (exFAT/FAT) through a full-speed hub passed write/read/persistence tests. Hub keyboard command input and USB card-reader mounting/listing also passed limited checks. `lsusb` shows native USB topology and mounts; see [diagnostics](docs/USB_DIAGNOSTICS.md) and [USB implementation](docs/USB_IMPLEMENTATION.md). Sustained input under load, high-speed mixed-speed hubs, FPGA communication, GUI extensions and full peripheral regression remain unverified.

USB Ethernet (RTL8153 CDC-ECM) passed link, DHCP, ping and SSH tests. The SSH server and client ran concurrently on hardware. Console ANSI/DEC extensions passed host tests; touch is being rechecked after adding ST7123/ST7121 input. USB storage reads and writes now pass alongside Ethernet ping without packet loss in the tested runs. C6 was updated to the official MCU application. Wi-Fi initialization, scanning, WPA2 association, DHCP, DNS and HTTP traffic passed on hardware with Ethernet unplugged. See [Ethernet](docs/USB_ETHERNET_IMPLEMENTATION.md), [SSH server](docs/SSH_SERVER_IMPLEMENTATION.md) and [terminal](docs/TERMINAL_IMPLEMENTATION.md) implementation records.

Native SL completed on hardware serial, including ANSI screen restoration and Ctrl-C/Escape reruns; visual quality and SSH animation remain unverified. Wi-Fi SFTP passed 102400-byte SD roundtrips, EXCL preservation and read-only denial with authenticated test clients. The actual helper/Windows OpenSSH 9 passed a 20480-byte SD transfer; actual Linux process/FUSE and USB network transfers remain unverified. See [SL](user/sl/README.md) and [storage helper](docs/STORAGE_HELPER.md).

## Planned work

- FPGA communication, DDR3 buffers, rectangle fills, image copies and GUI integration.
- Shared local/SSH terminal: VT100 baseline, required VT220 features and xterm extensions, key input and scrollback.
- USB UVC cameras; hardware validation and fixes for the existing Tab5 built-in camera driver.
- USB mouse, touch integration including ST7123, and BLE keyboard/mouse support after feasibility checks.
- Tab5 microSD, USB mass storage and FAT support; evaluate exFAT.
- Access Tab5 SD/USB through SFTP and a portable Linux helper; use existing SSHFS/FUSE for mounting or fall back to file transfer. See [storage helper](docs/STORAGE_HELPER.md) for verification scope and limitations.
- Initial native MicroPython v1.26.1 port with REPL/scripts/VFS and machine.Pin/ADC/PWM/UART/SoftI2C/SoftSPI and monotonic time bindings implemented; complete build and hardware REPL, Pin/ADC input and GC pressure checks passed; SD file/import, exception cleanup, MemoryError recovery and Ctrl-C/restart also passed; sustained operation and external output fixtures remain unverified. See [MicroPython scope and limitations](docs/MICROPYTHON.md).
- Investigate porting CPython and Pyxel to run existing games without source or asset preprocessing; validate compatibility and memory first, then FPGA rendering acceleration.
- Small TUI application ports; investigate nano/Vim dependencies. Linux executables do not run directly on MeOS.

Resource comparisons from matched SDK/toolchain rebuilds, artifact hashes and runtime-memory accounting are documented in [resource usage](docs/RESOURCE_USAGE.md) and shown on the [public page](https://airpocket-soundman.github.io/tab5-meos-fpga/#resources).

## Development

See the [detailed development record (Japanese)](docs/DEVELOPMENT_GOALS.md), [public roadmap](https://airpocket-soundman.github.io/tab5-meos-fpga/) and [preserved upstream documentation](docs/UPSTREAM_README.md).

The upstream Tab5 build entry is `sh tools/build.sh tab5 build` with ESP-IDF v5.4.4. This fork has reproduced native-DMA Tab5 builds and hardware flashing with ESP-IDF v5.4.4.

## Related projects

- [Upstream MeOS](https://github.com/shi3z/MeOS)
- [Linux FPGA research](https://github.com/airpocket-soundman/tab5-linux-fpga)
- [FPGA GPU](https://github.com/airpocket-soundman/tab5-fpga-gpu)
- [Tab5 / Tang Primer 20K carrier](https://github.com/airpocket-soundman/tab5-tang-primer20k-carrier)
- [Tab5 thin client](https://github.com/airpocket-soundman/Tab5_thinclient)

## Attribution and licensing

Forked from [shi3z/MeOS](https://github.com/shi3z/MeOS), starting at `a6a9f234e509737f9fe84fe13468a8ec78b60fc9`. Preserve upstream history, attribution and third-party licenses. Upstream redistribution terms need confirmation; this fork does not assign a new license to upstream code.

## Website

The bilingual site lives in `docs/site/`. Run `python tools/build_pages.py` after changing the roadmap (Japanese text comes from `docs/DEVELOPMENT_GOALS.md`; maintain its English counterpart in the generator). GitHub Actions generates and publishes the site when its sources change on `main`. Use the language link at the top of either page to switch languages; no JavaScript is required.

- [FPGA carrier compatibility and pre-fabrication requirements](docs/FPGA_CARRIER_REQUIREMENTS.md)
