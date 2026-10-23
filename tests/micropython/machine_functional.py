# Actual VM functional test; run ONLY in the host mock, never on Tab5.
from machine import Pin, ADC, PWM, UART, SoftI2C, SoftSPI
import time

def raises(kind, operation):
    try:
        operation()
    except kind:
        return
    raise AssertionError("expected exception")

p = Pin(16, Pin.OUT, value=1)
assert p.value() == 1
p.off()
assert p.value() == 0
p.on()
assert p.value() == 1
raises(OSError, lambda: Pin(16))
p.deinit()
p.deinit()
raises(OSError, lambda: p.value())
p = Pin(16, Pin.IN)
raises(OSError, lambda: p.value(1))
p.deinit()
a = ADC(16)
assert a.read() == 2048
assert a.read_u16() == 32776
a.atten(3)
assert a.read() == 2051
a.deinit()
raises(OSError, lambda: a.read())
raises(OSError, lambda: PWM(16, freq=12345))  # Injected host HAL failure.
p = Pin(16)
p.deinit()
w = PWM(16, freq=1000, duty_u16=32768)
assert w.freq() == 1000 and w.duty_u16() == 32768
w.freq(2500)
w.duty_u16(65535)
assert w.freq() == 2500 and w.duty_u16() == 65535
w.deinit()
raises(OSError, lambda: w.freq())
u = UART(1, tx=16, rx=17)
assert u.read(3) == b"ab"  # Mock makes partial progress, nonblocking.
b = bytearray(4)
assert u.readinto(b) == 1 and b[0] == 99
assert u.read(1) is None
assert u.read(0) == b""
assert u.write(b"abcdef") == 2
assert u.write(b"") == 0
assert u.flush() is None
raises(ValueError, lambda: u.read(4097))
raises(ValueError, lambda: u.readinto(bytearray(4097)))
raises(TypeError, lambda: u.readinto(b"x"))
u.deinit()
u.deinit()
raises(OSError, lambda: u.read(1))
u = UART(1, tx=16, rx=17, timeout=25)
assert u.read(4) == b"abc"
assert u.read(1) is None
assert u.write(b"abcdef") == 6
u.deinit()
i = SoftI2C(scl=17, sda=16)
assert i.scan() == [0x42]
assert i.readfrom(0x42, 3) == b"BCD"
assert i.writeto(0x42, b"hi") == 2
assert i.readfrom_mem(0x42, 0x1234, 2, addrsize=16) == b"BC"
assert i.writeto_mem(0x42, 1, b"hi") is None
raises(OSError, lambda: i.readfrom(0x41, 2))
raises(ValueError, lambda: i.readfrom(0x42, 4097))
raises(ValueError, lambda: i.writeto(0x42, b"x", stop=False))
i.deinit()
raises(OSError, lambda: i.scan())
s = SoftSPI(sck=16, mosi=17, miso=18, cs=19)
assert s.read(3, write=0x5a) == b"\xa5\xa5\xa5"
b = bytearray(3)
s.readinto(b, write=0xf0)
assert b == b"\x0f\x0f\x0f"
s.write(b"abc")
b = bytearray(b"abc")
s.write_readinto(b, b)
assert b == bytes([158,157,156])
raises(ValueError, lambda: s.write_readinto(b"12", bytearray(1)))
s.deinit()
raises(OSError, lambda: s.read(1))
assert time.ticks_diff(time.ticks_add((1<<30)-2, 5), (1<<30)-2) == 5
before = time.ticks_us()
time.sleep_ms(2)
assert time.ticks_diff(time.ticks_us(), before) >= 2000
raises(ValueError, lambda: time.sleep_ms(-1))
raises(ValueError, lambda: time.ticks_add(0, 1<<29))
# Deliberately leave live resources: host main must release all at VM exit.
abandoned = Pin(16, Pin.OUT, value=1)
abandoned_pwm = PWM(17)
print("PASS actual MicroPython VM bindings against mocked IO (not hardware)")
