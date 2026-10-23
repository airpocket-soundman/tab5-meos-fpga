"""Build the two static roadmap pages from the bilingual README and plan."""
from pathlib import Path
from html import escape
import re
import json

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'docs/site'

EN_STATUS = [
    ('OS / apps', 'Tasks, memory, IPC, M5EX and WASM exist', 'Validate Tab5 baseline and integrate GUI / FPGA completion.'),
    ('Local terminal', 'Buffer, auto-scroll, partial ANSI', 'Add scrollback, colors and shared local / SSH processing.'),
    ('SSH', 'Custom SSH, tabs, colors, 256 history lines per tab', 'Check compatibility, input, reconnect and storage channels.'),
    ('Wi-Fi / Tailscale', 'Networking exists; Tailscale scope not audited', 'Check stability, supported behavior and GUI settings.'),
    ('Wired keyboards', 'I2C accessory or USB boot keyboard', 'Add coexistence, hotplug and GUI key events.'),
    ('USB mouse', 'Not implemented', 'Add HID input, pointer, buttons, wheel and GUI events.'),
    ('Bluetooth input', 'Not implemented', 'Audit C6 transport / firmware, then BLE HID pairing and reconnect. Assess Classic separately.'),
    ('Touch', 'GT911 single-point and screen keyboard; no ST7123', 'Add ST7123 and GUI press / move / release events.'),
    ('microSD', 'Tab5 driver missing and disabled', 'Implement block reads / writes and error handling.'),
    ('USB storage', 'No Mass Storage driver', 'Add bulk transfers, BOT / SCSI, block access and removal handling.'),
    ('Filesystems', 'M5FS, VFS, RAM disk; dedicated flash persistence for some data', 'Add FAT and VFS storage integration; evaluate exFAT.'),
    ('Storage over SSH', 'Not implemented', 'Mount Tab5 SD / USB on the connected Linux server using FUSE.'),
    ('Local applications', 'MeOS apps; no Linux binary compatibility', 'Start with small TUI ports such as sl; audit nano / Vim dependencies.'),
    ('MicroPython', 'Integration / dependencies not audited', 'Embed REPL, scripts, interruption, cleanup and VFS access.'),
    ('CPython / Pyxel', 'Not implemented; feasibility unverified', 'Prioritize porting CPython and Pyxel to run existing source/assets without preprocessing; add FPGA rendering after compatibility is established.'),
    ('Python GPIO', 'Planned; not audited', 'Basic machine.Pin-compatible digital I/O and pull settings.'),
    ('Shell GPIO', 'User commands planned; not audited', 'List roles / owners; configure, read, write and release pins.'),
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
## Storage mounted from the SSH server
Use Tab5_thinclient as a reference. A FUSE helper on the connected Linux server mounts Tab5 microSD and USB storage as ~/sd and ~/usb. A separate SSH channel connects the helper to MeOS VFS. Linux and FUSE stay on the server. Audit Python/FUSE prerequisites, helper compatibility and reuse terms.
- Start with read-only SD access, then writes, attributes, create/truncate/rename/delete, directories and USB storage.
- Restrict exported roots and offer read-only/read-write access. Coordinate local/remote access and caches; define write completion and synchronization.
- Handle media absence/removal, SSH disconnect/reconnect, helper exit and unmount. Never report unfinished writes as successful.
- Verify file contents and interrupted transfers. Add SCP-style file transfer and remote image display.
## MicroPython and GPIO
Integrate MicroPython with MeOS tasks, memory, console and VFS. Audit FreeRTOS dependencies before adapting ESP32 ports or Tab5_thinclient code. Provide REPL, scripts, Ctrl-C, exceptions and cleanup; measure GC heap and task stack use.
Share a MeOS pin-management API between Python and the shell. Begin with digital input/output and pulls; evaluate PWM, ADC, interrupts, I2C and SPI later.
- Map external connectors to GPIO numbers, supported modes, reserved roles and current owners.
- Reject conflicts with LCD, PSRAM, Wi-Fi, storage, keyboards and FPGA.
- Proposed commands: `gpio list`, `gpio mode`, `gpio read`, `gpio write`, `gpio release` (not available yet).
- Aim for basic `machine.Pin` compatibility. Define output state and ownership on exit, exception and interruption; define shell pin lifetime.
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

progress = json.loads((ROOT/'docs/PROGRESS.json').read_text(encoding='utf-8'))

def progress_html(lang):
    labels = {'ja': {'verified':'実機確認（範囲限定）','active':'実装中・実機未確認','planned':'未着手'},
              'en': {'verified':'Hardware checked (limited)','active':'In development / unverified','planned':'Not started'}}[lang]
    title = '開発順序と進捗' if lang == 'ja' else 'Development order and progress'
    note = 'USB以降は上から順に進めます。状態は実装・確認結果に基づき更新し、全体の完了率や日程は未設定です。' if lang == 'ja' else 'USB and later work proceed in the order shown. Status reflects evidence; no overall percentage or dates are committed.'
    rows = ''.join('<li class="stage '+x['status']+'"><div><strong>'+escape(x['title'][lang])+'</strong><span class="status">'+labels[x['status']]+'</span></div><p>'+escape(x['detail'][lang])+'</p></li>' for x in progress['items'])
    return '<section id="progress"><h2>'+title+'</h2><p>'+note+'</p><ol class="progress-list">'+rows+'</ol></section>'

for ja in (False, True):
    lang='ja' if ja else 'en'
    title='Tab5 MeOS FPGA 開発計画' if ja else 'Tab5 MeOS FPGA Development Plan'
    subtitle='M5Stack Tab5上のMeOSに、Tang Primer 20Kによる描画処理と外部DDR3バッファを追加する計画です。実装状況、構成、検証項目を記載します。' if ja else 'Plan to add Tang Primer 20K rendering operations and external DDR3 buffers to MeOS on M5Stack Tab5. This document describes implementation status, architecture and validation requirements.'
    notice='基準ファームウェアのビルド・書き込み・起動とシリアルシェルを確認済みです。USB拡張は実装中。FPGA通信・GUI拡張などの予定機能は未検証です。' if ja else 'Baseline build, flashing, boot and serial shell verified. USB extensions are in development. FPGA communication and GUI extensions remain unverified.'
    prefix='../' if ja else './'
    body=markdown(plan) if ja else '<h2>Feature status & planned work</h2>'+table(EN_STATUS,['Area','Current source status','Planned work'])+markdown(en_details)
    # Source Markdown links resolve from GitHub, not from the deployed site.
    body=body.replace('href="docs/', 'href="https://github.com/airpocket-soundman/tab5-meos-fpga/blob/main/docs/')
    related=['tab5-linux-fpga','tab5-fpga-gpu','tab5-tang-primer20k-carrier','esp32p4-linux-fpga-memory','Tab5_thinclient']
    links=''.join(f'<a href="https://github.com/airpocket-soundman/{r}">{r}</a>' for r in related)
    doc=f'''<!doctype html>
<html lang="{lang}"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Tab5 MeOS FPGA — {'開発計画' if ja else 'Development roadmap'}</title><meta name="description" content="{subtitle}"><link rel="stylesheet" href="{prefix}style.css"><link rel="alternate" hreflang="en" href="https://airpocket-soundman.github.io/tab5-meos-fpga/"><link rel="alternate" hreflang="ja" href="https://airpocket-soundman.github.io/tab5-meos-fpga/ja/"></head>
<body><header><strong>Tab5 MeOS FPGA</strong><nav aria-label="{'ナビゲーション' if ja else 'Navigation'}"><a href="#progress">{'進捗' if ja else 'Progress'}</a><a href="#plan">{'開発計画' if ja else 'Roadmap'}</a><a href="{'../' if ja else './ja/'}" lang="{'en' if ja else 'ja'}" hreflang="{'en' if ja else 'ja'}">{'English' if ja else '日本語'}</a><a href="https://github.com/airpocket-soundman/tab5-meos-fpga">GitHub ↗</a></nav></header>
<main><div class="hero"><h1>{title}</h1><p class="lead">{subtitle}</p><div class="notice">{notice}</div><p>{'更新日' if ja else 'Updated'}: <time datetime="2026-10-23">2026-10-23</time></p></div>{progress_html(lang)}<article id="plan">{body}</article><section><h2>{'関連プロジェクト' if ja else 'Related projects'}</h2><div class="links">{links}</div></section></main><footer>Tab5 MeOS FPGA · airpocket-soundman · {'研究開発計画' if ja else 'Research roadmap'}</footer></body></html>
'''
    dest=OUT/('ja/index.html' if ja else 'index.html')
    dest.parent.mkdir(parents=True,exist_ok=True)
    dest.write_text(doc,encoding='utf-8')
(OUT/'.nojekyll').touch()
print('Built English and Japanese roadmap pages.')
