# Native USB camera implementation

Status: 2026-10-23. The connected camera passed **one-frame capture and BMP save**. This is an initial capture result, not a usable continuous-video implementation. BMP readback and nonuniform pixel content were verified; visual inspection remains pending.

## Implemented path

`drivers/usb_dma/native_uvc.c` implements UVC class requests and frame assembly above the original MeOS USB Buffer DMA host. It does not use a FreeRTOS USB host stack or TinyUSB at runtime. ESP-IDF supplies hardware register definitions and PHY/port initialization.

The current format is uncompressed YUY2 only. Enumeration reads a configuration descriptor of at most 4096 bytes, associates the selected VideoStreaming interface with its formats and alternate settings, and chooses the smallest supported even-width mode within 640×480. PROBE/COMMIT negotiation uses the UVC-version-specific control length. The stream selects an alternate setting with sufficient negotiated payload capacity.

The original DWC2 Buffer DMA engine supports High-Speed isochronous IN with one, two or three transactions per service interval. It programs the transaction count and appropriate starting DATA PID, retains a private aligned 3072-byte internal DMA buffer, and checks returned lengths before copying into application buffers. This is separate from the Full-/Low-Speed split-transfer support for keyboards. Isochronous camera traffic through a High-Speed hub's transaction translator is not supported.

UVC payload parsing checks header length, FID/EOF, error flags and frame capacity. Incomplete or invalid frames are discarded. A complete YUY2 frame is converted to RGB565 and passed to the shared BMP writer used by the built-in camera. The raw/output frames reside in PSRAM; the USB DMA bounce buffer remains internal. Capture is serialized, detach closes the video pipe, and the stream returns to alternate setting zero on ordinary completion when the device remains attached. Physical removal tests remain pending.

## Connected-device result

| Field | Observed value |
| --- | --- |
| USB device | `056e:700a` |
| UVC version | 1.0 |
| Capture format | YUY2, 160×120 |
| Alternate setting | 6 |
| Raw endpoint wMaxPacketSize | `0x1380` |
| Bytes per transaction | 896 |
| Transactions per service interval | 3 |
| Negotiated maximum payload | 2688 bytes |
| Result | One complete frame obtained and saved as BMP |

Capture currently takes approximately **8–10 seconds to obtain one complete frame**, with many invalid/incomplete-payload observations during attempts. A practical frame rate has not been achieved. The diagnostic dropped counter is updated while a frame is bad; it must not be interpreted as a count of distinct lost frames. Capture temporarily raises task priority and services USB in bounded busy bursts, which can delay other tasks. Display retirement therefore checks the actual DMA source even after its software wait deadline, avoiding a false timeout caused solely by delayed scheduling.

The 160×120 BMP was read back: 19113 of 19200 pixels were nonzero, pixel values varied, and the decoded RGB565 FNV checksum was `9c385858`. These checks establish decodable, nonuniform stored content. They do not establish correct colors, orientation or scene detail; visual inspection, sustained capture, preview and image-quality evaluation remain separate checks.

## Port recovery

During bring-up, both the baseline Buffer DMA engine and the ISO-extended engine encountered the same initial device-descriptor control-transfer timeout. Restoring the baseline engine alone did not resolve it; cycling USB-A VBUS did. This experiment does not identify the underlying device state, but it does not support attributing that timeout to the ISO additions alone.

The shell `usbreset` request exits active class sessions before the USB task invokes the board power API: OFF for 500 ms, then ON with a 250 ms settling delay. The board helper performs a locked read/modify/write on the **fixed system I2C bus**, expander `0x44`, output register `0x05`, bit 3. It preserves Wi-Fi/charger outputs and does not follow the external keyboard's selected I2C pins. A read failure prevents writing an unknown output latch. This is an explicit rescan operation, not a claim of reliable unattended hot-plug recovery.

## Commands and tests

```text
uvc status
uvc capture /sd/uvc-test.bmp
uvc preview
usbreset
```

`preview` currently captures one frame and displays it briefly; it is not a continuous video viewer. The application capture deadline is 10 seconds.

```powershell
python tests/usb_host/run_uvc_test.py --cc D:/tmp/tab5-zig013/ziglang/zig.exe
python tests/usb_host/run_buffer_dma_test.py --cc D:/tmp/tab5-zig013/ziglang/zig.exe
python tests/camera/run_tests.py --cc D:/tmp/tab5-zig013/ziglang/zig.exe
```

Tests exercise the actual 1037-byte descriptor fixture from `056e:700a`, truncation, 20000 deterministic mutations, payload framing/error/FID/overflow behavior, and actual DMA-engine register traces for ISO transaction multipliers 1–3. Shared BMP and fixed-system-I2C tests cover storage format/error handling and power-register updates. Host tests do not prove USB electrical timing or camera image correctness.

## Remaining scope

- **Implemented, hardware verification incomplete:** one-frame preview, visual image inspection, physical detach/reconnect and cleanup during capture.
- **Not provided as a working feature:** continuous capture at a practical frame rate; reliable long-duration capture and coexistence under load.
- **Unsupported:** MJPEG decoding, other compressed formats, USB camera audio, bulk-video transport, Full-/Low-Speed isochronous split transactions and general multi-camera capture.
- **Unverified:** other camera models, other resolutions/frame intervals, hub camera capture, image quality and sustained bandwidth.

Final display coexistence check: two framebuffers, 16 submitted updates and 16 DMA-confirmed retirements, zero errors/timeouts, and the back buffer remained available during UVC capture. Maximum observed batch duration was 1642072 microseconds under load; scheduling latency remains a limitation despite correct buffer retirement.

## Host startup with the Tab5 Keyboard accessory (2026-10-27)

`keyboard_start()` used to return after detecting the Tab5 Keyboard accessory (I2C `0x6D`) without starting the
USB host task, so nothing plugged into the USB-A port was ever enumerated (`lsusb` empty, no `usbkbd` task).
It now starts the USB host in both cases. After the change a camera `1bcf:28c4` enumerated (High-Speed,
YUY2 352x288, alt 11, payload 3060) and `uvc capture` / `uvc preview` produced frames. This is a different
camera from the `056e:700a` documented above.

## Frame loss was self-inflicted (2026-10-27)

With the `1bcf:28c4` camera (YUY2 352x288, payload 3060, alt 11) a capture took 340-820 ms and about half the
packets were counted as dropped. Counters per capture showed no error flags and no short frames at end-of-frame, only
about 210 data packets per frame (one frame about every 200 ms) and one of every two frames marked bad *before* its
end: the receive loop flagged the frame in progress as bad every 100 ms of busy polling and slept 1 ms, because
sleeping loses the isochronous data of the next microframes. At low frame intervals every frame outlasted 100 ms, so
10 and 5 fps requests never completed a frame in 10 s.

The loop now yields only at a frame boundary (right after an end-of-frame packet) and mid-frame only after 1 s. Every
end-of-frame then yielded a complete frame: 300-450 ms per capture including stream setup, about 3 fps from separate
captures and 2.8 fps in `camera.Camera(camera.USB).live()`. The camera's own frame period (about 200 ms) is the next
limit; `uvc interval N` (100 ns units) requests another frame interval for the next capture but did not change it
here. `uvc live [SECONDS]` shows a continuous view (any key stops it). Each capture still negotiates, selects the
alternate setting and closes the stream; keeping the stream open between frames would approach the camera's frame
period. The "dropped" counter counts packets while a frame was bad, not frames.

## Motion-JPEG and the isochronous receive path (2026-10-27)

Camera `1bcf:28c4` advertises MJPEG at 30 fps in every size up to 1920x1080, but raw YUY2 only at 5-30 fps depending on
the size (`uvc modes` lists both). The `mjpeg` module (see [MODULES.md](MODULES.md)) adds the MJPEG path:
`uvc size 640x480 mjpeg`, then `uvc live` or the MicroPython `Camera.set_size(w, h, "mjpeg")`.

Software decoder, `drivers/mjpeg`: baseline Huffman JPEG, 4:4:4/4:2:2/4:2:0/grey, restart intervals, frames without a DHT
segment, reduced-size decoding by 1/2, 1/4 or 1/8 inside the inverse DCT (the camera path asks for the largest
reduction that still fills the picture it will be shown at). `python tests/mjpeg/run_tests.py` builds it with
Visual Studio and compares it with Pillow on 185 cases (several sizes, subsamplings, qualities, restart markers, no-DHT
frames, malformed input): all pass. Pillow smooths chroma and the decoder replicates it, so the RGB thresholds are PSNR
limits (32-36 dB at full size, lower for 4:2:0 at 1/8), and a luma-only check (about 44 dB) guards the entropy decoder
and the IDCT. Decoding on the Tab5, measured with `uvc status`: 640x480 full size 107 ms (333 ms before the per-pixel
division and the dense IDCT were removed), 352x288 40 ms, 640x480 reduced to 320x240 41 ms, 1280x720 reduced to 640x360
108 ms.

What limits the frame rate is the isochronous receive loop, not the decoder. The camera sets the UVC error bit on many
frames (352x288 MJPEG: 31 of 69; 640x480: 74 of 88; 1280x720: 73 of 90; raw 640x480 YUY2: every one of 248, which is why
sizes above 352x288 never completed before). Those frames really are short: a missing end-of-image marker in 38 of 88
and none failed in the entropy decoder. An MJPEG frame is a burst of full 3060-byte payloads in consecutive microframes,
and the host re-arms one transfer per packet, so a gap of a single microframe loses data. The receive loop now keeps two
packet buffers and arms the next transfer before it looks at the packet just received: raw 640x480 went from 0 to 16
complete frames in 5 s. MJPEG still loses most frames to the error bit (a set bit is not treated as fatal for MJPEG;
the missing end marker is), so it is not faster than raw 352x288 (about 4 fps against 5.8). The next step is in the
HAL: two channels alternating odd and even microframes, so each one has 250 us to be re-armed.
