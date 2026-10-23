# Native Tab5 ADC and PWM HAL

`hal_io_analog.h` is a privileged hardware interface. The shared I/O owner layer
must claim each exposed pin first; this HAL does not authorize board pins.
ESP32-P4 has no supported internal DAC (`SOC_DAC_SUPPORTED` is absent in the
ESP-IDF5.4.4 P4 capabilities), so no internal DAC API is provided.

## ADC

`hal_io_adc_channel(pin, &unit, &channel)` reports the SoC mapping: ADC1 uses
GPIO16–23 and ADC2 uses GPIO49–54. Units are1/2 and channels start at0.
The public Tab5 extension allowlist is narrower: M5Bus16/17/18/19/51/52 and
PortA53/54. GPIO20/21 are RS485,22 is backlight,23 is touch, and50 belongs to
the extension keyboard path; the owner layer excludes reserved pins.

`hal_io_adc_read_raw(pin, attenuation, &raw)` performs one polled12-bit read.
Attenuation codes0/1/2/3 mean0/2.5/6/12dB. The result is0–4095, not millivolts.
There is no eFuse, offset self-calibration, or voltage calibration. A nominal
initial code2166 follows the existing P4 boot entropy setup; measurement
accuracy must be established with known voltages before interpreting results.
Do not infer a safe input voltage from the attenuation setting.

The owned pad becomes high impedance with digital input/output and pulls
disabled. The conversion snapshots/restores selected ADC unit registers,
shared power/clock fields and modified analog register fields. It never resets
the global ADC controller. IRQ exclusion serializes the operation with the
existing temporary ADC entropy source, bounded by a1ms conversion timeout plus
short configuration/restore overhead. No FreeRTOS ADC driver, lock, or heap is
used. Call from task context with interrupts enabled.

## PWM

`hal_io_pwm_set(pin, frequency_hz, duty_permille, &actual_hz)` reserves LEDC
low-speed timer1/channel1 for one simultaneous pin. Camera XCLK uses timer0/
channel0; the display backlight is currently plain GPIO22 and remains reserved.
The shared LEDC PLL clock stays80MHz, matching camera setup. Global LEDC reset
and clock shutdown are never performed.

Frequency is1Hz–40MHz with dynamically selected1–20bit resolution and an8-bit
fractional divider. Duty is0–1000. The returned integer frequency reflects
quantization; requested and actual frequencies can differ. Resolution and duty
precision decrease at high frequencies. `hal_io_pwm_stop(pin)` stops only the
reserved channel/timer, reconnects the GPIO output and drives it low. A second
pin returns `ERR_BUSY` until the first is stopped. Pin ownership is managed by
the caller separately from this timer reservation.

## Validation

Run `python tests/io/analog_run_test.py --cc PATH/TO/zig.exe` on the host.
Tests cover mapping, invalid arguments, ADC timeout propagation, PWM frequency
limits/dividers and single-pin resource ownership. A register model executes
the actual native ADC conversion function with successful, timeout and invalid
result outcomes for both units, checking common and unit-state restoration.
These tests do not establish electrical measurement accuracy, real conversion
completion, PWM waveform shape, or coexistence on physical hardware.

The P4 source compiles with the actual target GCC/ilp32f flags. Its undefined
symbols are ROM register/GPIO helpers, SoC registers, MeOS IRQ/time APIs and an
integer division helper; no high-level ADC/LEDC driver or FreeRTOS calls appear.
Register sequences use Espressif ESP-IDF5.4.4 HAL/LL definitions underApache-2.0;
the repository includes that license at `third_party/esp_idf_entropy/LICENSE`.

On 2026-10-23, device reads completed on both ADC units: GPIO16/ADC1 with
attenuation code0 returned raw2379, and GPIO53/ADC2 with attenuation code3
returned raw876. The shared I/O layer rejected non-ADC GPIO35 and reserved
internal pins. These are completion and routing checks on unreferenced inputs;
the raw values do not establish voltage accuracy, polarity, linearity, noise,
or calibrated voltage range. No reference voltage was applied and no PWM
waveform was measured. GPIO output was not enabled for these ADC checks.
