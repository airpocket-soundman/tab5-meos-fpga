# Actual MicroPython VM tests with mocked board IO

Run on Windows:

```text
python tests/micropython/run_machine_test.py --cc D:/tmp/tab5-zig013/ziglang/zig.exe
```

The target must have been configured once so that
`build-tab5-size/esp-idf/main/micropython_cpp.settings` exists. The runner uses
that file's pinned upstream core source list and target compiler to generate
fresh QSTR/module/root headers in a temporary directory. It does not alter target
build headers. Zig compiles the actual MicroPython core and actual MeOS machine,
PWM, UART, software bus and time bindings into an x86 Windows executable.
Pointers, long and MicroPython integers are checked to be 32 bits, matching the
ESP32-P4 port. Source-line and detailed error reporting are enabled only for host
diagnostics. `alloca.h` and the task declarations are Windows compatibility
headers, not replacement VM implementations.

Only the board IO services and monotonic clock are mocked. There is no physical
GPIO, UART, I2C or SPI traffic. Successful host tests establish VM argument,
buffer, exception and resource-lifetime behavior; they do not establish hardware
waveforms, timing accuracy or peripheral compatibility. Do not run
`machine_functional.py` on the Tab5: it intentionally requests output pins.

The functional suite exercises Pin output/input and owner conflicts, ADC reads,
PWM updates and constructor rollback after an injected HAL failure (mock frequency
12345), UART partial/nonblocking/deadline IO, I2C scan/memory/NACK, SPI overlapping
buffers and time wraparound. It leaves live Pin and PWM objects deliberately;
the C runner asserts that VM teardown released every owner, pin and bus.
Negative UART/PWM tests use the actual Python parser/compiler/VM. The separate
intentional RuntimeError case checks teardown after an uncaught Python exception.
There is no filesystem/import adapter in this harness: external open/import
hooks return ENOENT. `run_vfs_test.py` covers the production VFS adapter with a
separate C-API model, not this actual VM.

`--settings PATH` selects another configured target's settings.
`--work-dir PATH` preserves generated artifacts for debugging. An explicit
`--reuse-headers` skips header regeneration only when those artifacts are known
to match current source/configuration; normal verification must not use it.
Optional positional script paths replace the default suite.
