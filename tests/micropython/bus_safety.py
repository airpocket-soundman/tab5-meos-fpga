"""Safe on an attached Tab5: only rejected bus constructions/wrong-self calls.

This deliberately never creates a working software bus or drives external pins.
It does not validate electrical timing, ACKs, repeated START, or SPI data.
"""
from machine import SoftI2C, SoftSPI

count = 0
def rejects(call):
    global count
    try:
        call()
    except (TypeError, ValueError, OSError):
        count += 1
    else:
        raise AssertionError("invalid bus operation accepted")

# System I2C pins are reserved; all constructors below must fail before GPIO.
rejects(lambda: SoftI2C(scl=31, sda=32))
rejects(lambda: SoftI2C(scl=31, sda=32, freq=-1))
rejects(lambda: SoftI2C(scl=31, sda=32, freq=400001))
rejects(lambda: SoftI2C(scl=31, sda=32, timeout=-1))
rejects(lambda: SoftI2C(scl=31, sda=32, timeout=100001))
rejects(lambda: SoftI2C(scl=object(), sda=32))
rejects(lambda: SoftI2C(31, 32))
# SD pins are reserved, including when malformed mode/rate is supplied.
rejects(lambda: SoftSPI(sck=43, mosi=44, miso=39, cs=42))
rejects(lambda: SoftSPI(sck=43, mosi=44, miso=39, cs=42, baudrate=-1))
rejects(lambda: SoftSPI(sck=43, mosi=44, miso=39, cs=42, polarity=2))
rejects(lambda: SoftSPI(sck=43, mosi=44, miso=39, cs=42, phase=-1))
rejects(lambda: SoftSPI(sck=43, mosi=44, miso=39))
rejects(lambda: SoftI2C.deinit(None))
rejects(lambda: SoftI2C.scan(None))
rejects(lambda: SoftI2C.readfrom(None, 0x20, 1))
rejects(lambda: SoftI2C.writeto(None, 0x20, b"x"))
rejects(lambda: SoftI2C.readfrom_mem(None, 0x20, 0, 1))
rejects(lambda: SoftI2C.writeto_mem(None, 0x20, 0, b"x"))
rejects(lambda: SoftSPI.deinit(None))
rejects(lambda: SoftSPI.read(None, 1))
rejects(lambda: SoftSPI.readinto(None, bytearray(1)))
rejects(lambda: SoftSPI.write(None, b"x"))
rejects(lambda: SoftSPI.write_readinto(None, b"x", bytearray(1)))
print("bus safety PASS", count)
