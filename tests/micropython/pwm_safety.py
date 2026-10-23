# No valid PWM construction: this test must not drive any external GPIO.
from machine import Pin, PWM


def refused(call):
    try:
        call()
    except (OSError, ValueError, TypeError):
        return
    raise AssertionError("operation should have been refused")


for pin in (8, 32, 37, 39):
    refused(lambda: PWM(pin))
for pin in (-1, 55):
    refused(lambda: PWM(pin))
for frequency in (0, -1, 40000001):
    refused(lambda: PWM(16, freq=frequency))
for duty in (-1, 65536):
    refused(lambda: PWM(16, duty_u16=duty))
refused(lambda: PWM("16"))
refused(lambda: PWM(16, 1000))  # freq is keyword-only
refused(lambda: PWM.freq(123))
refused(lambda: PWM.duty_u16(123))
refused(lambda: PWM.deinit(123))

# A Pin object cannot alias a PWM object; an already claimed numeric pin must
# also be rejected before io_pwm_set is reached. Pin stays input throughout.
p = Pin(16, Pin.IN)
refused(lambda: PWM(p))
refused(lambda: PWM(16))
assert p.value() in (0, 1)
p.deinit()
# Constructor rejection must not leave a provisional pin reservation behind.
q = Pin(16, Pin.IN)
assert q.value() in (0, 1)
q.deinit()
print("MicroPython PWM no-output rejection safety PASS")
