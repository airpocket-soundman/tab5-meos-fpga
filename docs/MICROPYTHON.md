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
32-bit floating point, `gc`, `math`, `sys`, and basic `io`. It does not include
threads, networking modules, `os`, `time`, native-code emitters, or a package
installer. This is a trusted in-process runtime, not a security sandbox.

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
claim. Pin IRQs, PWM, I2C, SPI and UART Python bindings are not included yet.

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

The complete target build and flash passed. Firmware size was 1,396,080 bytes,
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
termination remain unverified. PWM, I2C, SPI, UART and IRQ Python bindings are
still absent; shell/native C support does not imply Python bindings exist.
