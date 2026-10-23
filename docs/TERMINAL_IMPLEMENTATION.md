# Terminal implementation and validation

The LCD console and the graphical SSH client implement an output-side subset of ANSI/DEC terminal controls. This supports the screen operations commonly used by curses applications such as remote `vim` and `sl`; it does not establish full VT100, VT220, or xterm conformance. Linux executables still run on the SSH peer, not directly on MeOS.

## Implemented output controls

| Area | LCD console | Graphical SSH client |
| --- | --- | --- |
| Cursor position, relative movement, line/display erase, insert/delete/erase characters | Yes | Yes |
| Save/restore cursor and rendition (`ESC 7/8`, CSI s/u) | Yes | Yes |
| Scroll margins, index/reverse index, insert/delete lines, scroll up/down | Yes | Yes |
| Origin mode, autowrap, insert mode, cursor visibility | Yes | Yes |
| Alternate screen modes 47, 1047, 1049; mode 1048 cursor save | Yes | Yes |
| SGR default/16 colors, 256-color palette, RGB colors | RGB565 display precision | RGB565 display precision |
| SGR bold/normal and reverse/normal | Bright palette rendition | Bright palette rendition |
| G0/G1 DEC special graphics and SI/SO | Unicode line drawing | Unicode line drawing |
| OSC/DCS/APC/PM strings | Consumed without executing content | Consumed without executing content |
| DSR status/cursor reports (`CSI 5n/6n`) | No reply channel | Queued SSH channel reply |
| Application cursor mode (`?1h/l`) | Output-only console | Arrow keys use SS3 in application mode |

`CSI 2J` erases the display without homing the cursor. `CSI 3J` clears retained history. Partial scrolling regions and alternate-screen output do not enter the primary scrollback history. Alternate buffers are allocated in PSRAM when first requested; failed allocation leaves the existing screen intact.

The LCD console keeps its native shell convention: LF starts the next line at column zero. The SSH renderer keeps the current column on LF, as expected by remote terminal software. The native LCD screen, color planes and retained lines use PSRAM rather than internal static RAM. If the screen allocation fails, the LCD console does not start; serial output remains available.

## Keyboard behavior for remote full-screen programs

While the SSH client is using its alternate screen, Ctrl-U, Ctrl-O, Ctrl-T, PageUp and F1–F4 pass to the remote application instead of triggering the usual local tab/history actions. The local Ctrl-backslash prefix remains available. Arrow key sequences follow application cursor mode. This is intended to preserve Vim editing and navigation shortcuts.

## Validation and remaining work

Host regressions compile the actual native parser and extract the actual SSH parser; the harness does not contain a second implementation. Tests check color storage, cursor restoration, margin isolation, reverse index, alternate-screen restoration/history isolation, DEC modes, DSR reports, control strings, numeric bounds, and 100,000 deterministic random bytes per parser. History tests check ring wrapping, color retention, viewport anchoring, resize behavior and absent-history fallback. Assertions are explicitly enabled when using an optimizing host compiler.

Run:

```powershell
python tests/console/run_parser_test.py --cc D:/tmp/tab5-zig013/ziglang/zig.exe
python tests/console/run_history_test.py --cc D:/tmp/tab5-zig013/ziglang/zig.exe
```

These are parser and history regressions. Hardware rendering, key transport and complete remote Vim/SL sessions require separate end-to-end verification. Underline/italic rendering, configurable tab stops, mouse reporting, bracketed-paste transport, full VT220 character sets, double-width/combining Unicode, and complete device-identification/query support remain outside this implementation. UTF-8 characters are stored as BMP single cells; unavailable font glyphs depend on the existing font fallback.

The small legacy windowed terminal in `apps/terminal/terminal.c` has its own simpler renderer and is not covered by these changes. The Tab5 LCD console and graphical SSH client remain separate parser implementations; common parser extraction is a later refactor.

Control behavior reference: [XTerm Control Sequences](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html).

## Tab5 touch-controller revisions

The touch reader probes ST7123-compatible controllers at I2C address `0x55` before falling back to GT911 at `0x14`. ST7121 and ST7123 display variants share the ST7123 touch-report layout. The firmware reads the controller coordinate range and maps portrait coordinates into the landscape LCD frame. The ST protocol uses a coordinate-present flag and valid bits in seven-byte contact reports; it does not use the GT911 status acknowledgement or wake sequence.

This corrects the case where the display worked on an ST7121 unit but the GT911-only touch probe failed and no on-screen-keyboard/touch polling task was started. The OSK task continues polling the console region when the keyboard is hidden. A downward drag reveals older console output; an upward drag returns toward live output, and a tap returns directly to live output.

Touch-report decoding tests use `python tests/console/run_touch_test.py --cc D:/tmp/tab5-zig013/ziglang/zig.exe`. They check probing, portrait mapping, a valid contact in a later report slot, release, read errors and GT911 fallback. Actual panel coordinates and gestures still require hardware confirmation.

Protocol reference: [M5Stack M5GFX ST7123 touch implementation](https://github.com/m5stack/M5GFX/blob/master/src/lgfx/v1/platforms/esp32p4/Touch_ST7123.inl).

## Buffered console redraws on Tab5

The DPI backend previously used one framebuffer while scanout continuously read it. A scrollback redraw changed glyph runs and rows in that active buffer one at a time, so a refresh could contain a partly updated viewport. The existing `display_present` also copies pixels into the active framebuffer; it does not switch scanout buffers.

The backend now requests two DPI-owned RGB565 buffers from the existing 4 MiB display reserve. A console update affecting more than one row renders into the inactive buffer, including its cursor and input overlay, and then selects the completed buffer for the next DPI DMA cycle. A MeOS display mutex serializes keyboard and other drawing calls during the batch. The previous buffer is not reused until the display DMA channel source address is observed inside the new framebuffer. Refresh callbacks alone do not establish buffer retirement. The channel is discovered by its framebuffer address, without assuming a fixed channel number. The internal-framebuffer branch of ESP-IDF's DPI `draw_bitmap` performs cache maintenance and updates its framebuffer index; using one of the driver's own buffers avoids its external-buffer copy and FreeRTOS semaphore branches. The second framebuffer does not allocate another GDMA channel.

A full framebuffer copy at batch start preserves pixels outside the console, including the on-screen keyboard. Single-row edits and cursor blinking retain direct rendering to avoid a full-screen copy for each key. If the second framebuffer cannot be allocated, the console keeps direct rendering. A stalled refresh disables subsequent buffer reuse and logs that condition. Atomic publication applies to multi-row console updates while refresh is operating; legacy direct drawing APIs can still change the active framebuffer.

Host tests use the actual rendering and transaction code with fake DPI completion events. They check batch publication, preservation of other pixels, old-buffer retirement, nested locking, failure handling, dirty-row minimization, and color runs. Actual flicker reduction, refresh timing and input latency require target observation.

```powershell
python tests/console/run_render_test.py --cc D:/tmp/tab5-zig013/ziglang/zig.exe
python tests/console/run_display_frame_test.py --cc D:/tmp/tab5-zig013/ziglang/zig.exe
```

Implementation reference: [ESP-IDF v5.4.4 DPI panel implementation](https://github.com/espressif/esp-idf/blob/v5.4.4/components/esp_lcd/dsi/esp_lcd_panel_dpi.c).

Offscreen drawing targets are scoped to the calling task and its monotonic task ID. Other console/keyboard tasks continue to draw to the display, and reuse of a terminated task's control block does not inherit its external buffer. A frame batch holds the display mutex through publication and retirement.

Target verification (2026-10-23): the ST7121 unit accepted touch scrolling, as confirmed by the operator. With two framebuffers enabled, runtime diagnostics recorded 12 submitted updates and 12 DMA-confirmed retirements, with zero queue errors/timeouts and the back buffer still available. The largest observed batch duration was 276156 microseconds; this includes scheduling delay and is not a frame-rate guarantee. Retirement continued after a camera preview. The operator was absent during the final display test, so subjective flicker reduction has not been confirmed.

Follow-up under UVC capture load: 16 submitted updates and 16 DMA-confirmed retirements, zero errors/timeouts, and the back buffer stayed available. A final DMA source check after the wait deadline prevents a delayed rendering task from reporting a false retirement timeout. Maximum observed batch duration was 1642072 microseconds, so high-load response latency remains unresolved.
