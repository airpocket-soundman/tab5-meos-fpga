# Shared external I2C and SPI

`io_i2c.h` and `io_spi.h` expose the same ownership-aware services to native
shell commands and the MicroPython `machine.SoftI2C` / `machine.SoftSPI` bindings. These are software-driven
external buses, separate from the board's system I2C implementation and SD
driver. They use the MeOS scheduler/mutexes and raw GPIO HAL; no FreeRTOS
task, ESP-IDF bus driver or DMA engine is required.

## Ownership and lifecycle

Create an owner with `io_owner_create()`, then open a bus with its owner ID.
Open atomically claims every requested pin through the common IO manager.
Duplicate pins and existing claims, including another bus belonging to the
same owner, are rejected. The board manager excludes reserved pins, including
system I2C GPIO31/32 and internal SD GPIO39/42/43/44. External keyboard pins
remain subject to the board's keyboard-detection reservation policy.

Each driver has four bus slots. Handles combine a slot with a monotonically
increasing generation; closing/reopening a slot does not make an old handle
valid. Owner and handle must match. Exhausting the generation counter returns
an error instead of wrapping. These IDs are trusted in-process resource
ownership, not memory isolation or a security boundary.

One mutex per driver serializes open, transfer and close operations, including
operations on separate buses of that driver. No interrupt mask is held across
wire transactions. Cancellation only marks a running transfer; the transfer
checks it during bit-level service. Owner cleanup marks cancellation before
waiting for the driver mutex and releases all its buses. Cleanup accepts an
owner already marked closing by the IO manager. Bus slots are invalidated before pins are released, so cleanup after a terminated
closer cannot touch a newly claimed pin. Pin release returns pins to
unclaimed inputs without pulls through that manager.

Calls are for normal task context, with the scheduler running and interrupts
enabled. They are not ISR-safe. A caller's buffers must remain valid until
transfer returns; receive buffers can contain a partial result on error.
Cancel does not roll back bytes already delivered to the external device.
An owner must be closed when an interpreter/session exits, including exception
or interruption paths. A forced task termination needs the same owner cleanup;
reclaiming a mutex alone cannot undo the external device's partial command.

## SoftI2C

```c
io_i2c_config_t cfg = {
    .sda = 5, .scl = 6, .frequency_hz = 100000,
    .stretch_timeout_us = 10000, .transfer_timeout_us = 2000000
};
uint32_t bus;
/* Pins are examples: the common manager must confirm they are exposed/free. */
int rc = io_i2c_open(owner, &cfg, &bus);
/* Check rc before continuing. One register write followed by a repeated START. */
uint8_t reg = 0x10, data[2];
if (!rc) rc = io_i2c_transfer(owner, bus, 0x50, &reg, 1, data, sizeof data);
/* Close a successfully opened bus, or close its owner during session cleanup. */
```

Addresses are seven-bit values. Write-only, read-only and write-then-read are
supported. Write-then-read emits a repeated START without an intervening STOP.
A zero-length transfer probes the address in write direction. Every written
byte checks the slave ACK; reads ACK all bytes except the final byte, which is
NACKed. An address/data NACK returns `IO_ERR_NACK`.

Both signals use open-drain release/drive-low operations. Each released SCL
waits for clock stretching, with a bounded timeout. Errors/cancellation attempt
a STOP and release both signals. If a slave holds SCL low, a valid STOP cannot
be guaranteed; cleanup waits only its bounded stretch limit and then releases
the pins. There is no nine-clock bus recovery or multi-master arbitration.
Use appropriate external pull-ups and compatible IO voltage; the HAL's weak
pull-up fallback is not a substitute for checking the physical bus.

Requested frequency is 1000–400000 Hz. Half-periods round upward to integer
microseconds, so the actual clock is no faster than requested; 400 kHz is
therefore not an achieved throughput claim. GPIO overhead and task preemption
can extend clock periods. The default stretch timeout is 10000 us (maximum
100000 us); the default transfer deadline is 2000000 us (maximum 10000000 us).
Zero selects either default. Each direction is limited to 4096 bytes.

## SoftSPI

The configuration claims separate SCK, MOSI, MISO and active-low CS pins.
MISO is an input without an internal pull. All four modes are supported:

| Mode | Clock idle | Input sampling edge |
| --- | --- | --- |
| 0 | Low | Rising |
| 1 | Low | Falling |
| 2 | High | Falling |
| 3 | High | Rising |

Words are eight bits, MSB first, with full-duplex input/output. `tx == NULL`
sends `0xff`; `rx == NULL` discards input. In-place transmit/receive works;
partially overlapping buffers are not a supported contract. CS is asserted
once per transfer and deasserted on success, timeout or cancellation. Clock
returns to its configured idle level. There is no API for retaining CS across
calls or sharing a single claimed SCK/MOSI/MISO bus among several bus objects.

Requested frequency is 1000–500000 Hz, with integer-microsecond half-period
rounding. Transfers contain at most 4096 bytes. The default transfer deadline
is 2000000 us, maximum 10000000 us; zero selects the default. This is bitbang
SPI: GPIO overhead/preemption reduces rate and introduces gaps, and devices
requiring uninterrupted clocks or strict maximum inter-byte delays need a
future hardware-peripheral implementation. Timing and throughput have not
been measured on an external device.

## Errors and validation

Both APIs return zero on success and a negative `ERR_*` on failure, including
invalid configuration, unavailable/reserved pins, owner mismatch, stale
handles and timeout. The shared IO extensions are `IO_ERR_NACK` (-19) and
`IO_ERR_CANCELLED` (-20). Cancel returns `ERR_BUSY` if the bus has no running
transfer. The transfer deadline excludes mutex queueing time; cleanup may add
up to one bounded stretch wait and several half-period delays. MeOS task
preemption can increase wall-clock latency beyond these CPU-service limits.

```powershell
python tests/io/i2c_spi_run_tests.py --cc D:/tmp/tab5-zig013/ziglang/zig.exe
```

Host tests include the production drivers with GPIO/wire traces. They verify
repeated START, read/write address directions, data ACK/final NACK, stretch
and total timeouts, cancellation cleanup, SPI modes 0–3, MSB/full-duplex data,
CS cleanup, owner checks, generation reuse, reserved-pin rejection and cleanup
after an owner enters closing state. Fault injection also kills a closer exactly
after pin release, then reclaims the same pins for another owner and verifies
that old cleanup makes no GPIO changes. A closing-owner/start race is tested
before the first wire operation. These tests validate sequencing and API
contracts; external wiring, electrical timing, sensor/device compatibility and
interpreter integration require separate target tests.


## Python binding validation scope

The bindings use numeric GPIO IDs, the same pin claims and generation-safe
handles, and VM-exit owner cleanup. The actual 32-bit MicroPython VM passed
functional/exception tests with board IO mocked. On Tab5, 23 rejection tests
passed without opening a valid external bus or performing external transfers.
These results do not verify sensor compatibility, pull-ups, clock stretching,
SPI edge timing or physical wiring. See [MicroPython APIs](MICROPYTHON.md) and
[the actual-VM test scope](../tests/micropython/MACHINE_TESTS.md).
