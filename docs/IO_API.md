# Shared external I/O API and shell

The public interface is in `kernel/include/m5os/io.h`, `io_i2c.h`, `io_spi.h` and `io_uart.h`. API version1 is a trusted, in-process C service. It is independent of the shell and does not require MicroPython. MicroPython itself is not embedded by this change.

## Lifetime and future bindings

Create a named owner with `io_owner_create`. Reserve all pins for an object atomically with `io_pins_claim`; configure/read/write digital pins through `io_gpio_*`. GPIO object deinitialization releases its pin. ADC uses `IO_USE_ADC` and `io_adc_read`; attenuation codes0/1/2/3 mean0/2.5/6/12dB. Raw values are12-bit, not calibrated millivolts. PWM uses `IO_USE_PWM`, `io_pwm_set` and `io_pwm_stop`; one PWM output is supported concurrently.

Protocol open functions claim their pins and return handles; close releases the object. Handles/tokens are not reused. Do not release a protocol's pins independently of its close function. A later `machine.Pin`/`ADC`/`PWM`/`SoftI2C`/`SoftSPI`/`UART` adapter can retain these tokens and pass Python-owned buffers directly for the duration of a call.

A VM soft reset/shutdown must call `io_owner_close`. Exception handlers and object `deinit()` must close their own resources. Owners record their creating task; `io_reap_dead_owners` performs deferred cleanup after a creator or interrupted closer has terminated. Run it in task context, never from interrupt-masked task termination. It runs when creating a new owner and before shell I/O commands. Worker tasks sharing an owner therefore share its creator's lifetime. This is ownership coordination, not process isolation.

I2C/SPI transfers return0 or negative errors. UART read/write return byte counts or negative errors; a partial byte count takes priority over a timeout. UART write acceptance is not wire completion: call flush. Map negative errors to Python exceptions in the later binding. No driver prints to the console or depends on Python types.

## Shell commands

`io help` prints complete syntax; numeric arguments accept decimal or `0x` hexadecimal. Transfer data is contiguous hexadecimal byte pairs, up to128bytes per shell call. Driver APIs permit larger bounded buffers. All shell I/O objects share the named Shell owner; `io reset` closes all of them.

```text
io caps
gpio list
gpio list all
gpio mode 16 in none
gpio read 16
gpio release 16
adc read 16 0
uart selftest
io reset
```

Output and external-protocol examples below require appropriate external wiring. They are not executed automatically:

```text
gpio mode 2 out 0
gpio write 2 1
gpio release 2
pwm set 2 1000 500
pwm stop 2
i2c open 53 54 100000
i2c scan HANDLE
i2c xfer HANDLE 0x20 00 2
i2c close HANDLE
spi open 5 18 19 35 100000 0
spi xfer HANDLE a55a
spi close HANDLE
uart open 6 7 115200
uart write HANDLE 48656c6c6f 100
uart flush HANDLE 100
uart read HANDLE 16 100
uart close HANDLE
```

Replace `HANDLE` with the number returned by open. Reserved/internal pins are rejected, even when no user object owns them. Pin read/write also requires the correct owner and mode. Ext.Port1 stays reserved when its keyboard is detected. Release restores high-impedance input without internal pulls.

## Implementation limits and evidence

- No general-purpose on-chip DAC; DAC is excluded. PWM is not a DAC.
- SoftI2C is a single-master bit-banged implementation with repeated START, ACK/NACK, clock stretching and bounded cleanup. SoftSPI supports8-bit MSB-first words, modes0..3, full duplex and automatic active-low CS. Requested frequencies are upper targets; scheduling can stretch timing. Hardware acceleration may later retain these APIs.
- UART1 supports polling8N1 at9600..1000000baud. No flow control, DMA, interrupt RX ring or guarantee of lossless high-rate continuous input.
- The connected Tab5 passed GPIO16 input/read and input-mode write rejection, internal-pin refusals, raw ADC reads on16/53, reset/release and UART internal loopback. The UART test does not configure or drive external pads.
- External output/PWM waveform, connected-device I2C/SPI and wired UART communication, reference voltages and calibration remain unverified without fixtures. Existing subsystem regressions are recorded separately.

See [analog](IO_ANALOG.md), [soft protocols](IO_PROTOCOLS.md) and [UART](IO_UART.md) for implementation details and tests.
