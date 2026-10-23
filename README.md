# Tab5 MeOS FPGA

English | [日本語](README.ja.md)

An experimental MeOS fork for a graphical M5Stack Tab5 environment with FPGA-assisted rendering. MeOS handles input, windows and application state; a Tang Primer 20K and its 128 MB DDR3 are intended to hold graphics assets and accelerate selected drawing operations.

**[Development plan & feature status — English / 日本語](https://airpocket-soundman.github.io/tab5-meos-fpga/)**

## Current status

Baseline and size-optimized firmware built and flashed; boot and serial shell verified on Tab5. microSD and USB FAT write/read/persistence tests passed; USB hub support is next. See [USB implementation](docs/USB_IMPLEMENTATION.md). FPGA communication, GUI extensions and full peripheral regression remain unverified.

## Planned work

- FPGA communication, DDR3 buffers, rectangle fills, image copies and GUI integration.
- Shared local/SSH terminal: VT100 baseline, required VT220 features and xterm extensions, key input and scrollback.
- USB UVC cameras; hardware validation and fixes for the existing Tab5 built-in camera driver.
- USB mouse, touch integration including ST7123, and BLE keyboard/mouse support after feasibility checks.
- Tab5 microSD, USB mass storage and FAT support; evaluate exFAT.
- Mount Tab5 storage on an SSH-connected Linux server through a server-side FUSE helper, plus file transfer and remote image display.
- MicroPython REPL, scripts and VFS integration; GPIO access from MicroPython and the native shell through shared pin management.
- Investigate porting CPython and Pyxel to run existing games without source or asset preprocessing; validate compatibility and memory first, then FPGA rendering acceleration.
- Small TUI application ports; investigate nano/Vim dependencies. Linux executables do not run directly on MeOS.

## Development

See the [detailed development record (Japanese)](docs/DEVELOPMENT_GOALS.md), [public roadmap](https://airpocket-soundman.github.io/tab5-meos-fpga/) and [preserved upstream documentation](docs/UPSTREAM_README.md).

The upstream Tab5 build entry is `sh tools/build.sh tab5 build` with ESP-IDF v5.4.4. This fork has not reproduced the build yet. Flashing requires a separate hardware and recovery check.

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
