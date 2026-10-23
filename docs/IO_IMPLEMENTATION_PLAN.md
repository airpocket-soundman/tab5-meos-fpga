# Tab5 external I/O implementation plan

Date: 2026-10-23. Goal: operate external I/O from the native shell and expose the same C service to a later MicroPython port.

## Findings

- ESP32-P4 has no general-purpose on-chip DAC (`SOC_DAC_SUPPORTED` is absent in the pinned ESP-IDF SoC capabilities). DAC is excluded as requested. The board audio codec is a different function, not a general voltage-output DAC.
- Existing MeOS GPIO helpers and bit-banged system I2C can be used without starting FreeRTOS. New external protocols must not change the existing I2C pin selection or use reserved system buses.
- The official Tab5 pin map identifies external signals and internal connections. The initial service exposes GPIO0/1/2/3/4/5/6/7/16/17/18/19/35/45/47/48/50/51/52/53/54 only. Ext.Port1 GPIO0/1/50 stay reserved if the keyboard accessory is detected. GPIO37/38 remain reserved for console/debug.
- System I2C 31/32, C6 connections 8..15, SD 39..44, LCD 22, touch 23, camera 36, audio 26..30 and RS485 20/21/34 cannot be claimed through this external service.
- External ADC candidates are 16..19 and51..54. Input is raw ADC data; calibrated voltage measurements are separate work. PWM is distinct from DAC and must use a different timer/channel from the camera clock.

Primary sources: [official Tab5 pin map](https://docs.m5stack.com/en/core/Tab5#pinmap), [pinned ESP-IDF ESP32-P4 capabilities](https://github.com/espressif/esp-idf/blob/v5.4.4/components/soc/esp32p4/include/soc/soc_caps.h). Installed SDK HAL/LL and current board drivers are the implementation references.

See [shared API and Shell commands](IO_API.md) for the binding contract and usage.

## Architecture and order

1. Common resource service (`m5os/io.h`): named owner tokens, atomic multi-pin claims, reserved-pin rejection, mode validation, explicit close/reset and error codes.
2. GPIO input/output and pull configuration. Release returns pins to input with no pulls. The input API is shared by shell and future bindings.
3. ADC raw sampling and PWM on dedicated low-level resources; no FreeRTOS driver calls.
4. SoftI2C and SoftSPI with explicit pin selection, stale-handle rejection, bounded transfers, cancellation and cleanup. Software timing is an initial implementation, not a throughput guarantee. Hardware acceleration can preserve the public API.
5. UART1 with GPIO-matrix routing, bounded FIFO polling and explicit close/flush. Its default GPIO10/11 routing conflicts with C6 and must never be used.
6. Shell adapters, automated failure/concurrency tests, target build and available hardware audits. Mark external electrical/loopback tests unverified when no fixture is connected.

## MicroPython contract

- No shell parsing, printing, Python types or allocation policy in the shared driver interfaces.
- A VM creates an owner with `io_owner_create`; `Pin`, `ADC`, `PWM`, `SoftI2C`, `SoftSPI` and `UART` objects retain owner/pin or generation handles.
- Owner tokens record their creating task. Deferred task-context cleanup reaps owners whose creator or interrupted closer has terminated; interrupt-masked task-kill code never blocks on protocol locks.
- Objects call close/deinit; VM shutdown or soft reset calls `io_owner_close`, which closes protocols before releasing pins. Binding code must call this cleanup on exceptions and termination. Owner tokens are trusted in-process resource identities, not a security sandbox.
- Byte buffers and lengths are passed directly; byte counts/negative errors map to Python results and exceptions. Cancellation and bounded timeouts support eventual interruption handling.
- Current firmware does not embed MicroPython. This work supplies its hardware service boundary; Python integration remains a separate milestone.

## Validation gates

- Reserved and duplicate pins rejected without hardware writes; partial claims rolled back; wrong owner/mode and stale handles rejected.
- Driver errors and cancellation leave I2C lines released, SPI CS inactive, UART stopped and claimed pins reusable.
- Host tests exercise the real implementations with fake GPIO/register/clock behavior and bounded timeouts.
- Compile/link on ESP32-P4 with the current USB/BLE/camera build. Inspect dependencies for accidental FreeRTOS calls.
- On the connected Tab5, use read-only diagnostics, reserved-pin refusal and internal peripheral tests where possible. Do not imply an ADC reference-voltage, external UART/SPI loopback or connected-I2C-device test was performed without a fixture.
- Commit verified units separately and publish EN/JA progress with explicit remaining limitations.

## Bounded hardware results (2026-10-23)

The combined ESP32-P4 firmware built and linked successfully. The connected Tab5 passed `io caps` and GPIO listing plus the following checks:

- Reserved GPIO31, system I2C 31/32, SD SPI43/44/39/42 and C6 UART10/11 configurations were rejected without reassignment.
- GPIO16 accepted input without pulls and read 0. Writing an input was rejected; release and owner reset passed.
- ADC16 at attenuation code 0 returned raw 2379; ADC53 at code 3 returned raw 876. These were floating inputs, so they establish acquisition only, not calibrated voltage accuracy. ADC35 was rejected as unsupported.
- UART internal loopback passed without driving external pins. This does not verify GPIO-matrix wiring, an external loopback cable or a connected UART device.
- Display remained active with 19/19 updates retired and zero errors/timeouts. Internal free memory was about 82 KiB with zero allocation failures; PSRAM free was about 19704 KiB at this snapshot.

Log: `io-hardware-audit.log` (local development capture). External I2C/SPI/UART fixtures, PWM waveforms, ADC reference voltages, electrical timing and sustained transfers remain unverified. The external-I/O roadmap item remains in progress. MicroPython is still not integrated.

## Subsystem regression after I/O operations

The internal camera produced a native CSI/ISP RGB565 frame after the GPIO/ADC/UART checks (sequence 1, DMA errors 0). Wi-Fi then retrieved an HTTP 200 response of 577 bytes. The final GPIO list showed external pins available after `io reset`. These are bounded smoke tests; long-duration mixed I/O workloads remain unverified.

Implementation commits: [`e22d8bf` shared IO services](https://github.com/airpocket-soundman/tab5-meos-fpga/commit/e22d8bf) and [`8b26e52` native shell](https://github.com/airpocket-soundman/tab5-meos-fpga/commit/8b26e52). See [API and shell syntax](IO_API.md) and [SoftI2C/SoftSPI contracts](IO_PROTOCOLS.md).
