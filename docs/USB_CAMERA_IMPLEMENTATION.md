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
