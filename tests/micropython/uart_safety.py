# Real-VM negative tests only: no successful external UART construction or TX.
from machine import UART

def rejects(call, kind):
    try:
        call()
    except kind:
        return
    raise AssertionError("UART invalid operation unexpectedly accepted")

rejects(lambda: UART(0, tx=16, rx=17), ValueError)
rejects(lambda: UART(1, 0, tx=16, rx=17), ValueError)
rejects(lambda: UART(1, 1000001, tx=16, rx=17), ValueError)
rejects(lambda: UART(1, tx=16, rx=16), ValueError)
rejects(lambda: UART(1, tx=-1, rx=17), ValueError)
rejects(lambda: UART(1, tx=55, rx=17), ValueError)
rejects(lambda: UART(1, tx=16, rx=17, timeout=60001), ValueError)
rejects(lambda: UART(1, tx=16, rx=17, timeout=-1), ValueError)
rejects(lambda: UART(1, tx="16", rx=17), TypeError)
rejects(lambda: UART(1, tx=16, rx=17, bits=7), TypeError)
rejects(lambda: UART(1, tx=16, rx=17, parity=0), TypeError)
rejects(lambda: UART(1, tx=16), TypeError)
# Backlight/touch are board-reserved: the registry rejects without driving.
rejects(lambda: UART(1, tx=22, rx=23), OSError)
rejects(lambda: UART.read(None, 1), TypeError)
rejects(lambda: UART.readinto(None, bytearray(1)), TypeError)
rejects(lambda: UART.write(None, b"x"), TypeError)
rejects(lambda: UART.flush(None), TypeError)
rejects(lambda: UART.deinit(None), TypeError)
print("PASS UART negative constructors and wrong-self guards; no external UART output")
