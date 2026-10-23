# Intentional failure. Run smoke.py afterwards: its first Pin16 creation must
# succeed, demonstrating that the VM exit hook released this abandoned object.
from machine import Pin
abandoned = Pin(16, Pin.IN)
assert abandoned.value() in (0, 1)
raise RuntimeError("intentional exception: verify next VM can claim GPIO16")
