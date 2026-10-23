# Camera host tests

On Windows, with Python and Zig:

```powershell
python tests/camera/run_tests.py --cc C:/path/to/zig.exe
```

No device or serial port is used. The runner checks:

- Production BMP codec against fixed RGB565/RGB555/BGR24 layouts, including row
  padding, bottom-up ordering, metadata offsets, short I/O, malformed headers,
  and close errors.
- Production bit-banged I2C against simulated GPIO and Win32 threads. It checks
  full transaction ownership, repeated START, atomic register bit updates, and
  concurrent 70-byte touch / two-byte SCCB reads.
- Production camera lifecycle extracted at test time, with a mocked sensor and
  capture backend: read-only probing, unknown-chip rejection, failed allocation
  cleanup, task ownership, fresh frames after restart, and preservation of memory
  if DMA cannot halt.

The hardware-facing HAL/LL register backend is not emulated by these tests.
Physical capture, ISP output, DMA/LCD coexistence, and timing need target tests.

BMP readback statistics are checked against a fixed RGB565 FNV-1a fixture,
all-zero and uniform nonzero images, and truncated/null input. A successfully
decoded uniform image is reported as uniform, without treating it as malformed.
