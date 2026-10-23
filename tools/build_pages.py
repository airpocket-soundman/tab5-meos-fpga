"""Build the two static roadmap pages from the bilingual README and plan."""
from pathlib import Path
from html import escape
import re
import json

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'docs/site'
progress = json.loads((ROOT/'docs/PROGRESS.json').read_text(encoding='utf-8'))
PROGRESS_BY_ID = {item['id']: item for item in progress['items']}

EN_STATUS = [
    ('OS / apps', 'Tasks, memory, IPC, M5EX and WASM exist; ESP32-P4 FP context isolation passed hardware and host tests', 'Integrate GUI / FPGA completion and continue regression tests.'),
    ('Local terminal', '256-line colored history; ANSI/DEC colors, margins, cursor and alternate-screen controls implemented; host tests passed', 'Verify Vim/sl on hardware; continue documenting unsupported VT220/xterm features.'),
    ('SSH', PROGRESS_BY_ID['ssh']['detail']['en'], 'Independent PTYs, exec and network SFTP validation remain pending.'),
    ('Wi-Fi / Tailscale', 'C6 updated to official MCU application; WPA2, DHCP, DNS and HTTP verified over Wi-Fi', 'Verify long-duration traffic and reconnect; Tailscale scope remains unaudited.'),
    ('Wired keyboards', 'I2C accessory or USB boot keyboard', 'Add coexistence, hotplug and GUI key events.'),
    ('USB mouse', 'Not implemented', 'Add HID input, pointer, buttons, wheel and GUI events.'),
    ('Bluetooth input', PROGRESS_BY_ID['bt']['detail']['en'], 'Verify paired HID keyboard/mouse input and reconnect; one peer only. Assess Classic separately.'),
    ('Touch', PROGRESS_BY_ID['touch']['detail']['en'], 'Verify sustained operation and subjective flicker reduction; integrate GUI events.'),
    ('microSD', 'Tab5 SPI and /sd FAT verified: list, 38-byte write/read, persistence after reset; /usb concurrent', 'Optimize speed; verify hot-plug, sustained transfers and errors.'),
    ('USB storage', 'Native DMA + FatFs: two exFAT/FAT drives passed write/read, reset persistence and isolation; hub keyboard command input and USB card-reader mount/listing checked; lsusb shows cached topology', 'High-Speed mixed-speed receiver input passed; repeat storage write/read on Buffer DMA and verify sustained input under load and hot-plug.'),
    ('USB Ethernet', 'RTL8153 CDC-ECM: link, DHCP, ping and concurrent SSH verified at USB2 High-Speed', 'Measure throughput and repeat storage coexistence checks on Buffer DMA; other adapters are unverified.'),
    ('USB cameras', PROGRESS_BY_ID['usb-camera']['detail']['en'], 'Inspect saved images, improve frame acquisition and verify preview, hot-plug and other devices.'),
    ('USB diagnostics / card reader', 'Native lsusb verified with a hub and three storage devices; USB microSD reader mounted and listed before/after reset without card writes', 'Add stable device identification; verify card-only replacement, multiple LUNs and reader writes. USB block nodes are not exposed under /dev.'),
    ('Built-in camera', PROGRESS_BY_ID['builtin-camera']['detail']['en'], 'Assess visual quality, sustained frame rate, long-duration operation and cleanup.'),
    ('Filesystems', 'M5FS, VFS, RAM disk, FAT/exFAT and selected flash persistence', 'Continue hot-plug/error and coexistence validation.'),
    ('Storage over SSH', PROGRESS_BY_ID['storage-sftp']['detail']['en'], 'Validate network SFTP and Linux FUSE; add reverse export and multiple incoming sessions.'),
    ('Local applications', 'MeOS apps; no Linux binary compatibility', 'Start with small TUI ports such as sl; audit nano / Vim dependencies.'),
    ('MicroPython', PROGRESS_BY_ID['micropython']['detail']['en'], 'Validate sustained operation; add missing modules and Python peripheral bindings separately.'),
    ('CPython / Pyxel', 'Not implemented; feasibility unverified', 'Prioritize porting CPython and Pyxel to run existing source/assets without preprocessing; add FPGA rendering after compatibility is established.'),
    ('Python GPIO', 'machine.Pin and uncalibrated ADC use shared IO ownership; GPIO16 input, ADC16/53, rejection checks and 40 GC pressure cycles passed on hardware', 'Add Python PWM/I2C/SPI/UART/IRQ separately and verify external electrical behavior.'),
    ('Shell GPIO / external IO', PROGRESS_BY_ID['external-io']['detail']['en'], 'Device reserved-pin/GPIO/ADC/internal-UART checks passed; validate external wiring, sensors, waveforms and loopback.'),
    ('ADC / PWM / DAC', 'Uncalibrated raw12-bit ADC; PWM uses one dedicated timer/channel; no general-purpose DAC on ESP32-P4', 'Measure reference voltages and PWM waveforms; audio codec is not a general voltage-output DAC.'),
    ('FPGA / DDR3', 'Not implemented in this fork', 'Transport, buffers, drawing primitives and GUI integration.'),
]

def inline(s):
    s = escape(s)
    s = re.sub(r'`([^`]+)`', r'<code>\1</code>', s)
    s = re.sub(r'\*\*([^*]+)\*\*', r'<strong>\1</strong>', s)
    return re.sub(r'\[([^\]]+)\]\(([^)]+)\)', r'<a href="\2">\1</a>', s)

def table(rows, headings):
    return '<div class="table-wrap"><table><thead><tr>' + ''.join('<th scope="col">'+inline(c)+'</th>' for c in headings) + '</tr></thead><tbody>' + ''.join('<tr>'+''.join('<td>'+inline(c)+'</td>' for c in r)+'</tr>' for r in rows) + '</tbody></table></div>'

def markdown(text):
    """Render the small, controlled Markdown subset used in this project plan."""
    out=[]; rows=[]; list_kind=None
    def close_list():
        nonlocal list_kind
        if list_kind: out.append('</'+list_kind+'>'); list_kind=None
    def close_table():
        if rows:
            out.append(table(rows[1:],rows[0]));rows.clear()
    for line in text.splitlines():
        if line.startswith('|'):
            cells=[c.strip() for c in line.strip('|').split(' | ')]
            if not all(re.fullmatch(r'[-: ]+',c) for c in cells): rows.append(cells)
            continue
        close_table()
        m=re.match(r'^(#{1,3}) (.*)',line)
        if m:
            close_list(); level=min(len(m[1])+1,4)
            out.append(f'<h{level}>'+inline(m[2])+f'</h{level}>');continue
        m=re.match(r'^(?:- |\d+\. )(.*)',line)
        if m:
            kind='ul' if line.startswith('- ') else 'ol'
            if list_kind!=kind:close_list();out.append('<'+kind+'>');list_kind=kind
            out.append('<li>'+inline(m[1])+'</li>');continue
        close_list()
        if line.strip():out.append('<p>'+inline(line)+'</p>')
    close_table();close_list()
    return '\n'.join(out)

plan=(ROOT/'docs/DEVELOPMENT_GOALS.md').read_text(encoding='utf-8')
# Operational checkout paths are not useful on the public site.
plan='\n'.join(s for s in plan.splitlines() if 'ローカル配置' not in s)
en_details='''## Architecture and milestones
MeOS manages input, windows, application state and drawing commands. Tang Primer 20K and its 128 MB DDR3 hold assets and accelerate selected drawing operations. Existing MeOS GUI code is the starting point. Windows compatibility is not a goal.
DDR3 is an explicitly transferred external data region, not transparent CPU RAM. Initially use the existing Tab5 LCD through ESP32-P4. Image readback can limit performance; direct FPGA display needs separate hardware investigation. Spill/restore and compute buffers remain supporting research topics.
1. Reproduce the Tab5 build; measure CPU rendering and RAM usage.
2. Verify FPGA round trips, DDR3 integrity and effective bandwidth.
3. Implement rectangle fills and image copies, then present results on Tab5.
4. Integrate CPU/FPGA rendering backends, input, windows and menus; evaluate resident assets and dirty regions.
5. Expand to compositing, scaling and compute acceleration based on measurements.
The first milestone is an interactive window and menu with at least one FPGA drawing operation. Speed improvement must be measured separately.
## USB implementation reference
The official M5Stack Tab5 UserDemo uses ESP-IDF USB Host/HID drivers with FreeRTOS tasks. Tab5thinclient uses TinyUSB Host HID/MSC APIs. This fork uses its native Buffer DMA driver with split transactions for mixed-speed hubs. Following TinyUSB 0.19.0, the native HAL now waits for host mode before configuring FIFOs. USB writes, complete readback and persistence after reset passed on the corrected driver. ESP-IDF provides PHY initialization and DWC2 HAL services; the FreeRTOS scheduler is not used. The TinyUSB OS_NONE/FIFO implementation remains available for comparison. See the [DMA investigation](docs/USB_DMA_INVESTIGATION.md).
References: [official Tab5 USB implementation](https://github.com/m5stack/M5Tab5-UserDemo/blob/main/platforms/tab5/main/hal/components/hal_usb.cpp), [TinyUSB DWC2 host](https://github.com/hathach/tinyusb/blob/0.19.0/src/portable/synopsys/dwc2/hcd_dwc2.c).
## USB hubs and simultaneous devices
Implemented one external hub, port power, connection-change/reset handling, address allocation, per-device DATA PID tracking and independent MSC mounts. Two exFAT/FAT drives on a full-speed hub passed 512/8192 byte write/read, persistence after reset, volume isolation and simultaneous SD access. Hub keyboard command input passed on a separate DMA channel; sustained input under load and storage hot-plug remain pending. A USB microSD reader was mounted and listed without writing the card. The native lsusb command shows cached topology and mounts. See the [hub record](docs/USB_HUB_IMPLEMENTATION.md) and [diagnostics](docs/USB_DIAGNOSTICS.md).
Execution order: USB receiver/storage, microSD, USB write correction and hub/multiple devices, concurrent SSH server/client, touch scrolling, Bluetooth keyboard/mouse.
USB 2.0 High-Speed hubs (480 Mbit/s) are an explicit requirement, including mixed high-speed drives and full/low-speed keyboards or receivers. Buffer DMA and Transaction Translator handling are implemented. The 0bda:5411 High-Speed hub enumerated two drives, a Full-Speed receiver and Ethernet; keyboard input and shell execution passed. Storage write/read regression on the new engine remains pending. See [Espressif limitations](https://docs.espressif.com/projects/esp-usb/en/latest/esp32p4/usb_host.html).
## Shared terminal and compatibility
Share terminal processing between the local console and SSH client. Implement VT100 as the baseline, then required VT220 features and selected xterm extensions. The SSH server must handle terminal type, PTY size, resize notifications and session I/O.
- Verify cursor movement/save/restore, line/screen erase, text attributes/colors, scrolling regions, autowrap and DEC special graphics.
- Implement arrows, editing/function/modifier keys and application cursor/keypad modes.
- Add alternate screen, 256/RGB colors, bracketed paste and mouse reporting as needed. Full VT220 or xterm compatibility is not yet promised.
- Integrate scrollback and touch input; test fragmented control sequences, UTF-8 and resizing. Match TERM and terminal replies to actual support; do not advertise xterm-256color prematurely.
- Verify with vttest and remote Vim, nano, less and top. Remote applications are separate from native application ports.
Reference: [XTerm Control Sequences](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html). Develop the common terminal alongside concurrent SSH, before touch scrolling.
## USB and built-in cameras
Native UVC supports uncompressed YUY2 using High-Speed isochronous Buffer DMA with one to three transactions per service interval. One 160×120 frame from 056e:700a was saved as BMP and read back. Continuous video, hot-plug and other devices are unverified; MJPEG, audio and bulk video are unsupported.
The native SC202CS CSI/ISP path passed capture, microSD BMP save, readback and restart. The USB UVC path passed one-frame capture and BMP readback but takes about 8–10 seconds per complete frame. Nonuniform stored pixels were verified; visual quality and sustained video operation remain unverified. The paths share BMP handling but retain separate capture transports. See the [native camera record](docs/CAMERA_IMPLEMENTATION.md) and [USB camera record](docs/USB_CAMERA_IMPLEMENTATION.md).
Keep the existing USB, microSD, hub, SSH, touch and Bluetooth priorities. Terminal compatibility belongs alongside SSH; camera work proceeds in parallel where dependencies allow. Set performance, format support and dates after measurement.
## Storage over SSH and Linux mounting
MeOS now provides a standard SFTP v3 subsystem restricted to /sd, /usb and /usb1–/usb3. Storage export defaults to read-only; change policy with sshd storage ro|rw|off only while the server is stopped. FAT exclusive creation is atomic and takes precedence over truncation; unsupported EXCL on M5FS/devfs is rejected.
The standalone Python standard-library/OpenSSH helper uses connect HOST ROOT MOUNTPOINT to try existing SSHFS/FUSE and falls back to interactive file transfer. Separate ls/get/put/mkdir/rm commands are also available. Mounts default to read-only and require --rw for writes. Normal SSH host-key checks and authentication remain in force; no automatic installation or sudo is used. See the [helper specification](docs/STORAGE_HELPER.md).
- Actual-engine, FAT-adapter, fragmented-wire, exclusive-create and partial/failure cleanup host tests passed, as did the complete target build. A local-engine SD hardware self-test passed a fresh 8192-byte write/read at /sd/sftp_00ddb817.bin, existing-file EXCL refusal and unchanged contents on reread. This test excludes SSH transport/authentication. Network SFTP and actual Linux FUSE mounting remain unverified.
- Linux must initially reach the Tab5 SSH server. Reverse export through an SSH connection initiated by Tab5 is not implemented.
- The SSH server currently accepts one inbound connection. A mount occupies it, preventing another remote Shell/transfer; the device local console remains usable. Independent channels and simultaneous access require further work.
- Rename, arbitrary truncation and unsupported attributes fail explicitly. Even write-enabled mounts may not support editor temporary-file replacement. put requires --overwrite for an existing destination and only succeeds when the server supports the requested operation.
- Validate media absence/removal, disconnect/reconnect, unmount, concurrent access and sustained transfers on hardware. Remote image transfer/display remains planned.
## MicroPython and GPIO
The initial native MicroPython v1.26.1 port connects MeOS tasks, memory, console and VFS without the upstream ESP32 FreeRTOS port or WASM. It provides REPL, file execution and python -c, a 512 KiB PSRAM GC heap and a 32 KiB task stack. The complete target build and flashing passed. Hardware REPL checks passed 2**80, math.sqrt(9) and GC. The input-only smoke.py passed GPIO16, ADC16/53, reserved/duplicate/released-object rejection and 40 GC pressure cycles. The SD script /sd/mpqa_6adb51fa.py passed sibling-module import (value123), exclusive 1024-byte binary file write/read, seek/tell, a 24-byte tail read and context-managed close. Pin reclaim in the next VM after an intentional RuntimeError passed, as did restart after a 1 MiB bytearray MemoryError and Ctrl-C interruption of an infinite loop followed by shell return/new VM execution. These verify the initial feature scope; long-duration use and external output waveforms remain unverified. Firmware size is 1,396,080 bytes, 165,280 bytes above the previous SFTP build. See the [MicroPython implementation record](docs/MICROPYTHON.md).
Shared owner/pin-management and shell services now cover GPIO, uncalibrated ADC, PWM, SoftI2C, SoftSPI and UART1. Actual-code host tests and the whole target build passed. Python machine.Pin and uncalibrated ADC bindings are implemented; Python PWM/I2C/SPI/UART and IRQ bindings remain pending. VM teardown closes the common IO owner and releases its pins. Input/ADC and GC-pressure execution passed on hardware; external output fixtures remain unverified. Device reserved-pin refusal, GPIO16 input/release/reset, raw ADC16/53 acquisition and UART internal loopback passed; ADC35 was rejected. Floating ADC inputs do not establish voltage accuracy. External electrical timing and fixture tests remain unverified; hardware bus acceleration and interrupt-driven GPIO are future work. General-purpose DAC is excluded because the SoC has none. See the [IO implementation plan](docs/IO_IMPLEMENTATION_PLAN.md), [shared API and shell](docs/IO_API.md), and [software protocol specification](docs/IO_PROTOCOLS.md).
- Map external connectors to GPIO numbers, supported modes, reserved roles and current owners.
- Reject conflicts with LCD, PSRAM, Wi-Fi, storage, keyboards and FPGA.
- Shell adapters provide `io help`, `io pins`, `gpio`, `adc`, `pwm`, `i2c`, `spi` and `uart`. Inspect ownership/reservations before changing a pin; use the help output for current syntax.
- Pin bindings reject duplicate objects, reserved pins, other owners and released objects. Objects abandoned to GC retain resources until VM exit; explicit deinit is recommended. Validate normal exit, exceptions, Ctrl-C and a fresh VM. Forced task termination is not a supported shutdown method.
- Verify valid/invalid pins, modes, pulls, ownership conflicts and repeated execution. Check wiring and voltage conditions before connecting hardware.
## CPython and Pyxel: existing games without preprocessing
The goal is to run published Pyxel Python source and assets without per-game C/C++ or WASM conversion or asset preprocessing. Porting CPython and Pyxel itself is the primary candidate, separate from the MicroPython/GPIO work. Full compatibility and smooth performance remain unverified.
- Pin CPython and Pyxel versions. Audit Python bindings, Rust standard-library/target support, SDL2/OpenGL and other dependencies. Investigate native execution on MeOS; existing browser WASM is not directly usable.
- Connect CPython memory, files and time to MeOS; adapt Pyxel display, input, audio and file interfaces. Distinguish interpreted game logic from native engine work.
- Start with the official Jump Game, then Platformer, using original Python and .pyxres files. Check held/pressed keys, transparency, flips, camera, tilemaps, mutable image banks, text, random numbers, music and effects.
- Measure startup/runtime peak memory for CPython, Pyxel, assets, frames and audio against 32 MB PSRAM and internal RAM limits. FPGA DDR3 remains explicit-transfer storage, not an automatic extension of the Python heap.
- Establish compatibility with CPU rendering before adding FPGA support. Preserve the original logical resolution and evaluate scaling, copies and compositing.
- Compare frames and game state against the PC version using the same inputs; check audio/input and measure frame time, latency and memory. Target each game's original update rate and report compatibility and acceleration separately.
- Record external Python/OS dependencies, Pyxel version differences, and game/asset usage and redistribution terms per title.
References: [Pyxel](https://github.com/kitao/pyxel), [Jump Game](https://github.com/kitao/pyxel/blob/main/python/pyxel/examples/02_jump_game.py), [Platformer](https://github.com/kitao/pyxel/blob/main/python/pyxel/examples/10_platformer.py).
## Validation and priorities
Measure command transfer, FPGA execution, image readback and LCD presentation together. Record FPS, input latency, CPU load, internal RAM/PSRAM use and traffic. Align inputs, precision and transfer conditions with the separate Linux repository; Linux drivers cannot simply be reused.
Prepare ESP-IDF v5.4.4 and reproduce `sh tools/build.sh tab5 build`; record artifact hashes. Before flashing, check existing firmware/C6 state and recovery. Check carrier pin conflicts, PARLIO, DMA, interrupts, cache synchronization and scheduler cooperation.
Advance input and microSD/FAT alongside GUI/FPGA work. Build common GPIO management before Python GPIO; expand storage mounting from SD reads to writes and USB. Stage mouse, terminals, BLE and app ports around dependencies. Set dates and performance targets after baseline measurements.
## Proposal: external Japanese font storage
Store Japanese fonts on microSD and load them into FPGA DDR3 during startup. This is a proposal, not an implemented or finalized design; Tab5 microSD, FPGA transport and DDR3 support are prerequisites.
- The current embedded font array is 467,155 bytes (about 456 KiB). Removing it mainly saves flash, not an equal amount of internal RAM.
- For CPU rendering, fetch required glyphs and cache them on Tab5; evaluate the existing roughly 9 KiB cache, indexing, compression access and transfer latency.
- For FPGA rendering, investigate direct glyph access in DDR3. A dedicated memory region does not require a RAM-disk filesystem; choose based on the common storage design.
- Keep a small built-in Latin font for startup/errors and provide fallback diagnostics for missing SD, invalid fonts or FPGA initialization failure.
- Preserve font attribution/licenses; check format, version, size and integrity. Measure startup time, cache hit rate, glyph latency and flash/RAM savings.
## Attribution and sources
Fork starting commit: `a6a9f234e509737f9fe84fe13468a8ec78b60fc9` from [shi3z/MeOS](https://github.com/shi3z/MeOS). Preserve upstream history, attribution and third-party licenses. Upstream redistribution terms need confirmation; no new license is assigned to upstream code.
Reference: [shi3z — MeOS article](https://note.com/shi3zblog/n/n15e659e90a23). Article and upstream hardware reports are not this fork's verification results.
'''


def progress_html(lang):
    labels = {'ja': {'verified':'完了・基本機能実機確認済み','regression':'実装完了・回帰確認待ち','host-tested':'一部実装・ホスト試験済み','active':'実装・実機検証中','planned':'未着手'},
              'en': {'verified':'Complete: core hardware checks passed','regression':'Implemented: regression checks pending','host-tested':'Partly implemented: host tests passed','active':'Implementation / device validation','planned':'Not started'}}[lang]
    title = '開発順序と進捗' if lang == 'ja' else 'Development order and progress'
    note = '完了は記載した基本機能の実機確認を指します。長時間試験や追加機能は残件として区別します。実装後に転送方式を変更したUSBは、回帰確認待ちとして表示します。' if lang == 'ja' else 'Complete means the documented core hardware checks passed. Stress tests and additional features remain separate follow-ups. USB features affected by the transfer-engine change are marked as awaiting regression checks.'
    rows = ''.join('<li class="stage '+x['status']+'"><div><strong>'+escape(x['title'][lang])+'</strong><span class="status">'+labels[x['status']]+'</span></div><p>'+escape(x['detail'][lang])+'</p></li>' for x in progress['items'])
    checked = sum(x['status'] == 'verified' for x in progress['items'])
    regression = sum(x['status'] == 'regression' for x in progress['items'])
    host = sum(x['status'] == 'host-tested' for x in progress['items'])
    active = sum(x['status'] == 'active' for x in progress['items'])
    summary = f'基本機能完了・実機確認済み：{checked}項目 ／ 実装完了・回帰確認待ち：{regression}項目 ／ 一部実装・ホスト試験済み：{host}項目 ／ 開発・検証中：{active}項目' if lang == 'ja' else f'Core hardware checks complete: {checked} items / Implemented, awaiting regression: {regression} items / Partly implemented, host-tested: {host} / In development: {active}'
    return '<section id="progress"><h2>'+title+'</h2><p>'+escape(summary)+'</p><p>'+note+'</p><ol class="progress-list">'+rows+'</ol></section>'


def implementation_records(lang):
    title = '今回の検証と実装記録' if lang == 'ja' else 'Implementation and validation records'
    commits = [('4be8303', 'FP context'), ('79ac3ce', 'Touch / display'),
               ('6d58268', 'Display retirement under load'), ('cd262af', 'Native CSI / ISP camera'),
               ('5f74e3e', 'Native USB camera'), ('b7b8e6c', 'BLE transport / scan'),
               ('e22d8bf', 'Shared external IO services'), ('8b26e52', 'External IO shell')]
    links = ' · '.join('<a href="https://github.com/airpocket-soundman/tab5-meos-fpga/commit/'+sha+'">'+label+'</a>' for sha,label in commits)
    note = '実機確認は各項目に記載した範囲に限ります。画質・ちらつきの目視評価、連続撮影、Bluetooth HID入力は未確認です。' if lang == 'ja' else 'Hardware results apply only to each stated scope. Visual quality/flicker assessment, sustained capture and Bluetooth HID input remain unverified.'
    audit_label = '最終監査・実機検証記録' if lang == 'ja' else 'Final audit and hardware validation record'
    audit = '<a href="https://github.com/airpocket-soundman/tab5-meos-fpga/blob/main/docs/VALIDATION_2026_10_23.md">'+audit_label+'</a>'
    api_label = '外部I/O共通APIとシェル仕様' if lang == 'ja' else 'Shared external IO API and shell specification'
    api = '<a href="https://github.com/airpocket-soundman/tab5-meos-fpga/blob/main/docs/IO_API.md">'+api_label+'</a>'
    storage_label = 'SFTP・Linuxヘルパー仕様' if lang == 'ja' else 'SFTP and Linux helper specification'
    storage = '<a href="https://github.com/airpocket-soundman/tab5-meos-fpga/blob/main/docs/STORAGE_HELPER.md">'+storage_label+'</a>'
    return '<section><h2>'+title+'</h2><p>'+note+'</p><p>'+links+'</p><p>'+audit+' · '+api+' · '+storage+'</p></section>'

for ja in (False, True):
    lang='ja' if ja else 'en'
    title='Tab5 MeOS FPGA 開発計画' if ja else 'Tab5 MeOS FPGA Development Plan'
    subtitle='M5Stack Tab5上のMeOSに、Tang Primer 20Kによる描画処理と外部DDR3バッファを追加する計画です。実装状況、構成、検証項目を記載します。' if ja else 'Plan to add Tang Primer 20K rendering operations and external DDR3 buffers to MeOS on M5Stack Tab5. This document describes implementation status, architecture and validation requirements.'
    notice='基準ファーム、浮動小数点コンテキスト、microSD、USB Ethernet、Wi-Fi、SSH同時動作、タッチスクロール、内蔵カメラの基本機能を実機確認済みです。USBストレージの新転送方式での回帰試験は残件です。USBカメラとBluetooth入力は一部動作を確認し検証を継続中、FPGA・GUI拡張は今後の計画です。' if ja else 'Core hardware checks passed for baseline firmware, FP context, microSD, USB Ethernet, Wi-Fi, concurrent SSH, touch scrolling and the built-in camera. USB storage regression on the new transfer engine remains pending. USB cameras and Bluetooth passed limited checks; full input/video validation remains pending. FPGA and GUI extensions are planned.'
    prefix='../' if ja else './'
    body=markdown(plan) if ja else '<h2>Feature status & planned work</h2>'+table(EN_STATUS,['Area','Current source status','Planned work'])+markdown(en_details)
    # Source Markdown links resolve from GitHub, not from the deployed site.
    body=body.replace('href="docs/', 'href="https://github.com/airpocket-soundman/tab5-meos-fpga/blob/main/docs/')
    related=['tab5-linux-fpga','tab5-fpga-gpu','tab5-tang-primer20k-carrier','esp32p4-linux-fpga-memory','Tab5_thinclient']
    links=''.join(f'<a href="https://github.com/airpocket-soundman/{r}">{r}</a>' for r in related)
    doc=f'''<!doctype html>
<html lang="{lang}"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Tab5 MeOS FPGA — {'開発計画' if ja else 'Development roadmap'}</title><meta name="description" content="{subtitle}"><link rel="stylesheet" href="{prefix}style.css"><link rel="alternate" hreflang="en" href="https://airpocket-soundman.github.io/tab5-meos-fpga/"><link rel="alternate" hreflang="ja" href="https://airpocket-soundman.github.io/tab5-meos-fpga/ja/"></head>
<body><header><strong>Tab5 MeOS FPGA</strong><nav aria-label="{'ナビゲーション' if ja else 'Navigation'}"><a href="#progress">{'進捗' if ja else 'Progress'}</a><a href="#plan">{'開発計画' if ja else 'Roadmap'}</a><a href="{'../' if ja else './ja/'}" lang="{'en' if ja else 'ja'}" hreflang="{'en' if ja else 'ja'}">{'English' if ja else '日本語'}</a><a href="https://github.com/airpocket-soundman/tab5-meos-fpga">GitHub ↗</a></nav></header>
<main><div class="hero"><h1>{title}</h1><p class="lead">{subtitle}</p><div class="notice">{notice}</div><p>{'更新日' if ja else 'Updated'}: <time datetime="2026-10-23">2026-10-23</time></p></div>{progress_html(lang)}{implementation_records(lang)}<article id="plan">{body}</article><section><h2>{'関連プロジェクト' if ja else 'Related projects'}</h2><div class="links">{links}</div></section></main><footer>Tab5 MeOS FPGA · airpocket-soundman · {'研究開発計画' if ja else 'Research roadmap'}</footer></body></html>
'''
    dest=OUT/('ja/index.html' if ja else 'index.html')
    dest.parent.mkdir(parents=True,exist_ok=True)
    dest.write_text(doc,encoding='utf-8')
(OUT/'.nojekyll').touch()
print('Built English and Japanese roadmap pages.')
