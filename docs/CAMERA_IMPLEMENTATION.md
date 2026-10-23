# Built-in camera and BMP files

The Tab5 camera backend uses the SC202CS configuration already included in this
repository: SCCB address `0x36`, chip ID `0xeb52`, 1280×720 RAW8, one CSI lane at
576 Mbit/s. A different chip ID is reported and rejected before sensor setup
registers are written. Product descriptions alone do not select the sensor.

`camera_probe()` temporarily enables XCLK, reads ID registers `0x3107/0x3108`,
then stops XCLK unless the camera was already initialized. It does not write a
sensor initialization table, start streaming, allocate frames, or start DMA.

## Native backend

`drivers/camera/camera_native_tab5.inc` uses ESP-IDF 5.4.4 HAL/LL register helpers
for LEDC, CSI, ISP, and DW-GDMA. The high-level `esp_cam_ctlr`, `esp_isp`, and LEDC
driver calls have been removed from the camera path. Camera completion is polled
by `camera_frame()`; no FreeRTOS queue, task, ISR callback, or driver mutex is used.

- GPIO36 XCLK is approximately 23.981 MHz from LEDC's 80 MHz source, channel/timer 0.
- ISP converts RAW8/BGGR to RGB565 with demosaic, CCM, gamma, and color processing.
- Two cache-aligned 1280×720 RGB565 buffers use about 3.52 MiB of PSRAM.
- DW-GDMA **channel 3 is reserved for this backend**. The display uses the same
  DW-GDMA controller; camera setup never resets or disables that controller and
  never reconfigures the display channel. Additional DW-GDMA users must honor
  this reservation; a general shared channel allocator is not implemented here.
- CPU interrupt propagation is disabled for camera DMA; channel status is polled.
  A frame timeout ends the stream. Polling and file I/O can reduce delivered
  frame rate below the sensor's configured 30 fps.
- Stop disables the CSI bridge and requests channel disable. If needed, it issues
  a bounded abort to channel 3 only. If DMA ownership cannot be released, buffers
  are retained and restart is rejected until reset. Memory is never freed while
  DMA may still own it.
- Initialization failures release allocations and stop XCLK. `camera_deinit()`
  releases camera resources after a successful stop; the preview app and snapshot
  helper call it on exit.
- A streaming task owns the camera. Another task cannot reuse its stream, read
  its live frame pointer, or stop it. Returned frame memory is borrowed and must
  not be retained across subsequent frame operations or deinitialization.

The register sequences follow Espressif's ESP-IDF 5.4.4 CSI/ISP/DW-GDMA
implementations under Apache-2.0. Copyright and adaptation attribution are in the
source; the Apache license is included at `third_party/esp_idf_entropy/LICENSE`.

## Shared I2C

`hal/common/i2c_bitbang.c` serializes each complete transaction, including a
register read's repeated START. Register bit updates hold the same mutex across
the read and write. This prevents touch reads from interleaving SCCB camera
register accesses. Before the scheduler starts, board initialization needs no
mutex. The functions are for task context, not interrupt handlers.

## BMP format

`camera_image_save_rgb565()` is shared by built-in and USB camera applications.
It writes a top-down, 16-bit RGB565 BITFIELDS BMP with four-byte row padding.
Short writes are continued; write and final close errors are reported.

The image loader supports uncompressed 24-bit BGR, 16-bit BI_RGB/RGB555, and
16-bit RGB555/RGB565 BITFIELDS, with top-down or bottom-up rows. It checks DIB
size, planes, dimensions, masks, compression, offsets, truncation, and integer
boundaries. Metadata may extend beyond the initial header buffer. Unsupported
compression and pixel masks are rejected.

## Validation

```powershell
python tests/camera/run_tests.py --cc C:/path/to/zig.exe
```

Host tests cover independent BMP layout fixtures, odd-width padding, short I/O,
close failure, malformed images, camera lifecycle failure/ownership cases, and
two concurrent simulated SCCB/touch readers. Camera source is compiled separately
with the target toolchain as well. These tests do not prove physical sensor,
CSI/ISP clock timing, frame correctness, LCD coexistence, or measured frame rate;
those require explicit device capture and inspection.
On 2026-10-23, the connected Tab5 reported chip ID `0xeb52`, received a native
RGB565 frame with sequence 1 and zero DMA errors, and saved a 640×360 snapshot
on microSD as a 460870-byte BMP (70-byte header plus 460800 pixel bytes).
A subsequent camera restart/frame test also passed with zero DMA errors.
The display remained active with two framebuffers and no reported queue errors
or completion timeouts. These checks establish capture/save/restart operation;
image quality and sustained frame rate have not been measured.
