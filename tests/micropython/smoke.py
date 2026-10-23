# Device smoke test: input GPIO16 and uncalibrated ADC16/53 only.
# No output GPIO, PWM, bus transactions or file modifications.
import gc
from machine import Pin, ADC


def refused(call):
    try:
        call()
    except (OSError, ValueError, TypeError):
        return
    raise AssertionError("operation should have been refused")


for reserved in (8, 32, 37, 39):
    refused(lambda: Pin(reserved, Pin.IN))
refused(lambda: Pin(-1))
refused(lambda: Pin(55))
refused(lambda: Pin(16, 999))
# An output with pull is rejected before resource claim/configuration.
refused(lambda: Pin(16, Pin.OUT, Pin.PULL_UP))
refused(lambda: Pin.value(123))
refused(lambda: Pin.init(123))
refused(lambda: ADC.read(123))

p = Pin(16, Pin.IN)
assert p.value() in (0, 1)
refused(lambda: Pin(16, Pin.IN))
refused(lambda: p.value(1))
refused(lambda: p.on())
refused(lambda: p.off())

# Keep the Pin alive through explicit collections and allocation pressure.
for iteration in range(40):
    transient = [bytes(256) for _ in range(32)]
    gc.collect()
    assert p.value() in (0, 1)
del transient
p.init(Pin.IN, None)
p.deinit()
p.deinit()  # idempotent
refused(lambda: p.value())
q = Pin(16, Pin.IN)
refused(lambda: p.value())  # old object must not alias the newly claimed pin
q.deinit()

for gpio in (16, 53):
    a = ADC(gpio, atten=ADC.ATTN_0DB)
    raw = a.read()
    scaled = a.read_u16()
    assert 0 <= raw <= 4095
    assert 0 <= scaled <= 65535
    refused(lambda: ADC(gpio))
    gc.collect()
    assert 0 <= a.read() <= 4095
    a.atten(ADC.ATTN_11DB)
    assert 0 <= a.read() <= 4095
    a.deinit()
    a.deinit()
    refused(lambda: a.read())
    print("ADC GPIO", gpio, "raw", raw, "u16", scaled, "uncalibrated")

refused(lambda: ADC(35))
refused(lambda: ADC(16, atten=99))
print("MicroPython machine.Pin/ADC input and GC smoke PASS")

