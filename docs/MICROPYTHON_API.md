# MicroPython API reference

The modules available in the MeOS MicroPython port, with their signatures. Limits, ownership rules and what has been
checked on hardware are in [MICROPYTHON.md](MICROPYTHON.md). Start it with `python` (REPL), `python /sd/file.py` or
`python -c "..."` (the shell limits a command line to 127 bytes).

## Built-in modules

| module | contents |
| --- | --- |
| `gc` | `collect`, `enable`, `disable`, `isenabled`, `mem_alloc`, `mem_free`, `threshold` |
| `sys` | `sys.path`, `sys.platform` (`"meos"`), `sys.exit()` |
| `math` | MicroPython's `math` (32-bit floats) |
| `io` | basic streams; `open()` uses the MeOS VFS (`/sd`, `/usb`, `/home`, ...) |
| `time` | monotonic time only, no wall clock |
| `machine` | `Pin`, `ADC`, `PWM`, `UART`, `SoftI2C`, `SoftSPI` |
| `camera` | built-in and USB cameras, images, display (module `mp_camera`) |

## open()

`open(path, mode="r")`: modes `r`, `w`, `a`, `x` with optional `+`, `b` / `t`. Eight files and source readers can be open at
once. Methods: `read`, `readinto`, `readline`, `write`, `seek`, `tell`, `close`, iteration, `with`. `flush()` is not
supported: close the file. Use `with` or `close()`; files abandoned to the garbage collector stay open until the VM exits.

## time

| call | |
| --- | --- |
| `sleep(seconds)` | 0 to 86400 s |
| `sleep_ms(ms)`, `sleep_us(us)` | non-negative integers |
| `ticks_ms()`, `ticks_us()` | wrap at `1 << 30` |
| `ticks_diff(a, b)` | signed difference within half the period |
| `ticks_add(ticks, delta)` | `-(1 << 29) <= delta < (1 << 29)` |

## machine

Pins are GPIO numbers. A pin that the system uses (display, storage, Wi-Fi, ...) or that another owner has is refused with
`OSError`. `deinit()` releases a pin; the VM releases everything when it exits.

### Pin

`Pin(id, mode=None, pull=None, *, value=None)`

| member | |
| --- | --- |
| `Pin.IN`, `Pin.OUT` | modes |
| `Pin.PULL_UP`, `Pin.PULL_DOWN` | input pulls |
| `init(mode, pull, *, value)` | reconfigure |
| `value([v])` | read, or set an output |
| `on()`, `off()` | drive an output high / low |
| `deinit()` | release the pin |

No interrupts (`irq`).

### ADC

`ADC(id, *, atten=ADC.ATTN_0DB)`: uncalibrated 12-bit.

| member | |
| --- | --- |
| `ADC.ATTN_0DB`, `ATTN_2_5DB`, `ATTN_6DB`, `ATTN_11DB` | attenuation |
| `read()` | raw 0 to 4095 |
| `read_u16()` | the raw value scaled to 16 bits |
| `atten(a)` | change the attenuation |
| `deinit()` | release the pin |

### PWM

`PWM(pin, *, freq=1000, duty_u16=0)`: `pin` is a number, not a `Pin`. One PWM output at a time (it shares a timer with the shell
`pwm` command).

| member | |
| --- | --- |
| `freq([hz])` | 1 to 40000000 Hz; returns the actual frequency |
| `duty_u16([v])` | 0 to 65535 |
| `deinit()` | stop and release the pin |

### UART

`UART(1, baudrate=115200, *, tx, rx, timeout=0)`: UART1 only, 8N1, no flow control, `tx` and `rx` are different GPIO numbers.
`timeout` is a total deadline in ms (0 to 60000); 0 polls once.

| member | |
| --- | --- |
| `read([nbytes])` | up to 4096 bytes; `None` if nothing arrived |
| `readinto(buf[, nbytes])` | count, or `None` |
| `write(buf)` | up to 65536 bytes; the count accepted by the TX FIFO |
| `flush()` | wait until the FIFO and the serializer are idle |
| `deinit()` | close, discarding queued data |

### SoftI2C

`SoftI2C(*, scl, sda, freq=100000, timeout=10000)`: software bus, 1000 to 400000 Hz, 7-bit addresses, at most 4096 bytes per
transfer. `timeout` is the clock-stretch limit in microseconds.

| member | |
| --- | --- |
| `scan()` | list of responding addresses |
| `readfrom(addr, n, stop=True)` | read `n` bytes |
| `writeto(addr, buf, stop=True)` | write; `stop=False` is not supported |
| `readfrom_mem(addr, memaddr, n, *, addrsize=8)` | 8 or 16-bit register address |
| `writeto_mem(addr, memaddr, buf, *, addrsize=8)` | |
| `deinit()` | release the pins |

### SoftSPI

`SoftSPI(*, sck, mosi, miso, cs, baudrate=100000, polarity=0, phase=0)`: software bus, 1000 to 500000 Hz, 8-bit MSB first,
active-low chip select (each call asserts it once), at most 4096 bytes per call.

| member | |
| --- | --- |
| `write(buf)` | |
| `read(n, write=0)` | `write` is the byte sent while reading |
| `readinto(buf, write=0)` | |
| `write_readinto(out, in_buf)` | |
| `deinit()` | release the pins |

## camera

Cameras are the registered devices `camera0` (built-in) and `camera1` (USB), see [DEVICES.md](DEVICES.md). The same code runs
on either.

### Module functions

| call | |
| --- | --- |
| `camera.ONBOARD`, `camera.USB` | the two camera sources |
| `list()` | names of the cameras registered now, e.g. `["camera0", "camera1"]` |
| `available(source)` | true if that camera is present |
| `load(path, scale=1)` | read a BMP into an `Image`; `scale` keeps every n-th pixel (1 to 16) |
| `show(img, x=0, y=0, w=<screen>, h=<screen>, hold_ms=0)` | same as `Image.show` |
| `close()` | give the text console back after `show()` / `live()` without `hold_ms` |
| `usb_available()`, `usb_capture(timeout_ms=10000, scale=1)`, `onboard_capture(width=320, height=180)` | one-shot helpers |

A source is `camera.ONBOARD`, `camera.USB`, `"onboard"`, `"usb"`, `"camera0"`, `"camera1"` or `"/dev/camera1"`.

### Camera

`Camera(source=camera.ONBOARD)`: opens the camera; `OSError` if it is absent or busy. One handle per camera.

| member | |
| --- | --- |
| `name`, `source`, `width`, `height` | attributes: native frame size |
| `read(width=0, height=0, timeout_ms=10000)` | an `Image`; with no size, the smallest integer reduction that fits 400 x 300 |
| `read_into(image, timeout_ms=10000)` | scale into an existing image, no allocation |
| `modes()` | `[(width, height, "mjpeg" / "yuy2" / "raw", fps), ...]` |
| `set_size(width, height, format=None)` | choose a mode; `None` = MJPEG when offered |
| `set_flip(mirror, vflip=False)` | orientation |
| `live(duration_ms=5000, x, y, w, h, hold_ms)` | show frames as they arrive; returns the frame count; Ctrl-C stops |
| `close()` | release the camera; also `with Camera(...) as cam:` |

### Image

`Image(width, height)`: a black RGB565 image. Pixels live on the 512 KiB MicroPython heap.

| member | |
| --- | --- |
| `width`, `height` | attributes |
| `data` | the pixels: little-endian RGB565 `bytearray` of `width * height * 2` bytes, writable in place |
| `show(x=0, y=0, w=<screen>, h=<screen>, hold_ms=0)` | fit into the area keeping the aspect ratio (at most 960 x 540) |
| `save(path)` | RGB565 BMP, absolute path |
