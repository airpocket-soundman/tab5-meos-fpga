# MicroPython on MeOS

This is a native MeOS port of MicroPython v1.26.1, pinned to commit
`647c8b96cae7e202c7a020395b7cfe65e5b8ce04`. The interpreter, source compiler and
garbage collector are linked into the firmware. It does not use the upstream
ESP32 FreeRTOS port. It is independent of the WASM runtime and does not provide
CPython or Pyxel compatibility.

## Entry points

```text
python
python /sd/example.py
python -c "print(1 + 2)"
```

The foreground VM has a 512 KiB PSRAM GC heap and a separate 32 KiB MeOS task
stack. Only one VM is allowed. The shared shell waits until it exits. Ctrl-C
interrupts Python execution; Ctrl-D at the friendly REPL returns to the shell.
The normal shell's 127-byte command limit also applies to `-c`; longer programs
belong in files. Script-relative file access and imports use the script's
directory. REPL-relative file paths start at `/`.

The initial configuration includes source compilation, arbitrary-size integers,
32-bit floating point, `gc`, `math`, `sys`, basic `io`, and a monotonic `time`
module. It does not include threads, networking modules, `os`, native-code
emitters, or a package installer. This is a trusted in-process runtime, not a security sandbox.

## Files and resources

`open` uses MeOS VFS, with eight slots shared by files and source readers.
Binary/text streams provide read, readinto, readline, write, seek, tell, close,
iteration and context management. Modes `r`, `w`, `a`, `x`, optional `+`, and
`b`/`t` are supported. Explicit `w` truncates; `x` uses atomic FAT exclusive
creation. File offsets are limited to the signed 32-bit VFS range.

Close reports filesystem errors. The VFS has no standalone sync operation, so
writable `flush()` is explicitly unsupported; close the file to flush it.
Objects abandoned to GC retain a slot until VM exit: use `with` or explicit
`close()`. VM teardown closes every registered file even after a Python
exception, and releases all pins owned by the VM. An exception caught inside a
running script leaves its resources owned until explicit release or VM exit.
Forced task termination is not a supported VM shutdown mechanism.

## GPIO and ADC

```python
from machine import Pin, ADC
p = Pin(16, Pin.IN)
print(p.value())
p.deinit()
a = ADC(16, atten=ADC.ATTN_0DB)
print(a.read(), a.read_u16())
a.deinit()
```

`Pin` supports IN/OUT, input pulls, initial output value, `init`, `value`, `on`,
`off` and `deinit`. ADC reports uncalibrated 12-bit raw values; `read_u16` scales
that range to 16 bits. Attenuation constants select 0, 2.5, 6 or 11 dB.
Reserved pins, conflicting owners and duplicate objects are rejected by the
same C services used by the shell. Released objects cannot access a later
claim. PWM and UART bindings are described below. Pin IRQs remain unsupported;
SoftI2C/SoftSPI share the same IO ownership; see below.

## PWM (hardware output unverified)

`PWM(pin, *, freq=1000, duty_u16=0)` accepts a numeric GPIO ID, not a `Pin`
object. It claims the pin through the shared owner service. There is one
simultaneous PWM resource (LEDC timer1/channel1), also shared with native shell
PWM; another active pin is rejected. Valid construction configures an external
output, including a constant level when duty is zero.

- `freq()` returns the actual integer frequency reported by the native service;
  `freq(hz)` accepts 1–40000000 Hz and reapplies the current duty request.
- `duty_u16()` returns the cached requested value; `duty_u16(value)` accepts
  0–65535. The shared C service rounds requests to 0–1000 permille, with further
  timer resolution limits. This does not provide measured 16-bit duty accuracy.
- `deinit()` stops PWM and releases the pin, and may report hardware errors.
  VM teardown releases an abandoned PWM object's ownership.

Argument conversion/validation precedes claims or updates. Failed hardware
initialization releases its provisional claim, and setters check object/VM
liveness again after Python conversions. `tests/micropython/pwm_safety.py`
constructs only invalid, reserved or already-claimed requests, plus GPIO16
input objects; it deliberately never starts a valid PWM output. Real PWM
waveforms and connected-load operation remain unverified.

## UART (external communication unverified)

`UART(1, baudrate=115200, *, tx=TX_GPIO, rx=RX_GPIO, timeout=0)` requires explicit,
different numeric TX/RX GPIO IDs. It supports UART1 only, 9600–1000000 baud,
fixed 8N1, polling and no flow control. Default UART1 IOMUX GPIO10/11 belongs
to C6 SDIO and is reserved; the binding uses the same pin registry as the shell.
Opening configures TX idle high. A `Pin` object cannot alias a UART pin.

- `read([nbytes])`: maximum/default 4096, returning bytes or `None` when no bytes
  arrived before the deadline. A zero-length request returns empty bytes.
- `readinto(buffer[, nbytes])`: at most 4096 bytes and no more than the writable
  buffer length; returns a count or `None` for no progress.
- `write(buffer)`: maximum 65536 bytes, returning the count accepted by the TX
  FIFO, or `None` for no progress. It does not imply wire completion.
- `flush()` waits for both TX FIFO and serializer idle. An immediate busy or
  expired deadline raises `OSError`; `deinit()` closes/cancels and discards
  queued data, so flush first when completion is required.

Timeout is a total deadline of 0–60000 ms; zero performs one nonblocking poll.
Partial progress takes precedence over timeout/error. Longer operations use
native slices of at most 10 ms and check pending interruption between slices;
the current port coalesces polls, so Ctrl-C response can take roughly 320 ms
plus scheduling delay. Readline, `any`, UART IRQ, DMA and CTS/RTS bindings are
not implemented. Polling may lose high-rate input. The native C driver's
internal loopback hardware result does not verify this Python binding or an
external wired connection.

## Software I2C and SPI (physical communication unverified)

`SoftI2C(*, scl=GPIO, sda=GPIO, freq=100000, timeout=10000)` supports numeric
pins, 1000–400000 Hz, seven-bit addresses and at most 4096 bytes per direction.
Timeout is the clock-stretch limit in microseconds (0 selects 10000; maximum
100000). It provides `scan`, `readfrom`, `writeto`, `readfrom_mem`,
`writeto_mem` and `deinit`. Memory transfers support8/16-bit addresses;
combined native transfers use a repeated START. `stop=False` is unsupported.

`SoftSPI(*, sck=GPIO, mosi=GPIO, miso=GPIO, cs=GPIO, baudrate=100000,
polarity=0, phase=0)` requires a distinct numeric active-low CS pin, supports
1000–500000 Hz, eight-bit MSB-first transfers and modes 0–3. Methods are
`write`, `read`, `readinto`, `write_readinto` and `deinit`; each transfer is
limited to 4096 bytes and owns one CS assertion. read/readinto accept a fill
byte through `write=0`. There are no hardware I2C/SPI Python aliases.

Both are scheduler-dependent software buses. Actual32-bit VM tests used mocked
board IO; the Tab5 safety suite rejected 23 invalid/reserved/conflicting
requests without opening a valid bus. Physical peripheral communication,
pull-ups, clock stretching and edge timing remain unverified. Native timing
and transfer limits are documented in [IO_PROTOCOLS.md](IO_PROTOCOLS.md).

## Monotonic time

`time.sleep(seconds)` accepts a finite value in 0–86400 seconds.
`sleep_ms(ms)` and `sleep_us(us)` require nonnegative integers fitting the
port's signed integer range (up to 2147483647). Millisecond portions yield to
the MeOS scheduler and check pending interruption; sub-millisecond delay is a
bounded busy wait. Timing is scheduler dependent.

`ticks_ms()` and `ticks_us()` wrap at a period of `1 << 30`.
Use `ticks_diff(a, b)` for signed differences within half that period, and
`ticks_add(ticks, delta)` with `-(1 << 29) <= delta < (1 << 29)` for wrap-safe
addition. Negative/out-of-period ticks are rejected. This module has no wall
clock, RTC, epoch timestamps, calendar conversion or `ticks_cpu` API.

## Build and validation

Tab5 builds enable `M5OS_MICROPYTHON` by default; `-DM5OS_MICROPYTHON=OFF` omits
the interpreter and shell command. Pinned sources are vendored under
`third_party/micropython`, with the upstream MIT license. Build-time header
generation uses the target compiler and upstream Python generators, without
network downloads or POSIX shell utilities.

Hardware and host validation results are recorded in the development progress
page. `tests/micropython/smoke.py` uses input GPIO16 and ADC16/53 only;
`exception_cleanup.py` intentionally abandons a pin and raises an exception.
Run the smoke test in a new VM after that exception to check teardown.

### Tab5 hardware checks, 2026-10-23

The following results apply to the initial Pin/ADC port, before the new PWM,
UART and monotonic-time extensions. The complete target build and flash passed.
Firmware size was 1,396,080 bytes,
165,280 bytes above the preceding SFTP build. The following initial-scope checks
passed on the connected Tab5:

- REPL evaluation of `2**80`, `math.sqrt(9) == 3.0`, and explicit garbage
  collection (reported free GC heap: 515472 bytes in the tested run).
- The input-only machine smoke test: GPIO16 input, ADC16/53 raw/scaled reads,
  reserved/duplicate/released-object rejection, and 40 collection/allocation
  pressure cycles. Floating ADC inputs do not establish voltage accuracy.
- `/sd/mpqa_6adb51fa.py` execution and sibling-module import (`value == 123`).
  A newly and exclusively created binary file received 1024 bytes
  (`bytes(range(256)) * 4`); readback, seek/tell at offset1000, the remaining
  24-byte tail and context-managed close passed.
- An intentional `RuntimeError` abandoned a GPIO16 input object. A new VM
  reclaimed GPIO16 successfully, verifying exception-exit pin cleanup.
- A 1 MiB `bytearray` request raised `MemoryError`; a fresh VM then ran.
- Ctrl-C interrupted an infinite Python loop with `KeyboardInterrupt`, returned
  to the MeOS shell, and a fresh VM executed successfully.
- Twelve consecutive VM restarts each reported 515,712 bytes free after GC.
  After the tests, the kernel reported 82 KiB internal heap free and 19,704 KiB
  PSRAM heap free, with zero allocator failures. The existing 8 KiB SFTP SD
  self-test also passed with MicroPython included in the firmware.

These results cover the initial native VM, file and input/ADC functionality.
External output waveforms, connected sensors, long-duration use and forced task
termination remain unverified. The peripheral/time additions have the separate results below; IRQ is not
implemented. Physical output/bus behavior requires its own verification.


### Peripheral/time extension checks, 2026-10-23

The complete expanded target build and flash passed. Firmware size was
1,402,544 bytes, adding 6464 bytes to the initial MicroPython firmware.
The actual 32-bit MicroPython interpreter and real machine/time binding code
passed functional and exception-cleanup tests with IO mocked (156 native
service calls and eight UART bytes accepted by the mock). This includes PWM
setter/constructor-failure cleanup, UART partial/deadline IO, I2C NACK/memory
and SPI buffers; it is not an electrical peripheral test. See
[the exact actual-VM harness scope](../tests/micropython/MACHINE_TESTS.md).

On the Tab5, `pwm_safety.py` passed without constructing a valid PWM output,
`uart_safety.py` passed without TX configuration/traffic, and 23 software-bus
safety checks passed without valid bus traffic. The time range, wraparound and
delay suite passed. Ctrl-C returned from `time.sleep(60)` in about 0.125 seconds.
Native IO driver regression tests also passed. Physical PWM waveforms, wired
UART, real I2C/SPI devices and long-duration behavior remain unverified.
