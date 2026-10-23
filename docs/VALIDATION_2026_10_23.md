# Validation record — 2026-10-23

Source implementation through `b7b8e6c`. Hardware: M5Stack Tab5 (ESP32-P4), SC202CS internal sensor and USB camera `056e:700a`. This record describes bounded development checks, not full compatibility or long-duration certification.

## Build and independent audit

- ESP32-P4 application compiled and linked with the native Buffer DMA USB backend and optional MeOS NimBLE host enabled; the resulting application was flashed and tested.
- Separate reviewers checked display buffer ownership/retirement, camera DMA cleanup, floating-point context layout, HCI locking, allocation/entropy, pairing consent and input routing.
- Corrected findings include missing floating-point register preservation, cross-task display target ownership, delayed-task retirement checks, shared IME serialization and stale pairing consent.
- The selected 169 upstream NimBLE files match their recorded SHA-256 values in the committed Git index. Licenses are retained. The port avoids direct NimBLE calls into FreeRTOS or newlib stdio; existing IDF/WASM dependencies elsewhere are separate.

## Automated checks

Context frame/stack initialization audits; terminal parser/history/touch/renderer/frame-transaction tests; BMP format/readback and camera lifecycle failure tests; shared-I2C transaction and fixed-system-bus power-bit tests; real UVC descriptor truncation/mutation and payload tests; Buffer DMA register traces including HS ISO transaction multipliers 1–3; BLE boot HID/GATT policy, NPL, allocator, UUID formatter, asymmetric Hosted framing and enabled/disabled CLI tests passed.

## Hardware checks

| Area | Observed result |
| --- | --- |
| Floating-point context | Two workers completed 64 iterations each, zero failures; final concurrent build observed 466 ticks. |
| Built-in camera | SC202CS ID `eb52`; native CSI/ISP RGB565 frame; 640×360 BMP saved to microSD and read back; capture after stop/restart passed, DMA errors zero. |
| Built-in BMP readback | 230400 decoded pixels, all nonzero, nonuniform; decoded-pixel FNV-1a `07d53fa4`. |
| USB camera | Native UVC 1.0 YUY2 160×120, payload 2688, alternate 6, raw MPS `0x1380`; repeated single-frame BMP captures saved and decoded. |
| USB camera with BLE active | Final capture decoded 19200 pixels and was nonuniform (FNV-1a `f9ab5469`); no peer was connected. |
| Display with USB capture | Two buffers; final 17 submissions/17 DMA-confirmed retirements, zero errors/timeouts, back buffer retained. Maximum observed batch latency about 1.642 seconds under load. |
| BLE controller/discovery | Initialization reached ready/error 0. Three-second passive scans found nearby advertisements. No automatic peer connection. |
| BLE transport | Initial audited scan: 20 TX/22 RX, zero send failures/rejected packets. |
| Wi-Fi coexistence | HTTP 200 with 577-byte response after BLE initialization and scanning. |
| Memory after final coexistence run | Internal free 67 KiB, largest 63 KiB; PSRAM free about 19704 KiB; allocator failures zero. |

## Remaining verification

- Display flicker improvement has not been visually confirmed by the user. Correct buffer retirement does not remove scheduling latency.
- USB camera acquisition still takes seconds and discards many payloads; practical continuous video, image quality/orientation, physical hotplug during capture, hub capture and other formats/models remain unverified. MJPEG/audio and Full-/Low-Speed ISO split transport are unsupported.
- BLE pairing, physical keyboard/mouse input and reconnect require a selected test device. Current connection management is one peer; bonds are RAM-only. Classic Bluetooth and general HID Report Map support remain outside this implementation.
- USB storage/keyboard regressions on the latest combined transfer engine require the corresponding devices to be connected. Existing earlier results are retained separately.
- Remote Vim/sl, exhaustive VT220/xterm behavior and long-duration mixed workloads remain pending.

Raw local serial logs are not published because they can contain network identifiers and authentication URLs. Public progress reports retain only the relevant measurements.
