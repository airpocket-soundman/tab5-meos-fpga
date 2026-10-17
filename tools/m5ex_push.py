#!/usr/bin/env python3
"""Send a file to the running M5OS shell with its `put` command (base64 over serial).

  tools/m5ex_push.py /dev/cu.usbmodemXXXX build/foo.m5ex /apps/foo.m5ex [--run]
"""
import base64, sys, time, zlib, serial

port, src, dest = sys.argv[1:4]
run = "--run" in sys.argv
data = open(src, "rb").read()
s = serial.Serial(port, 115200, timeout=0.2)

def wait_prompt(timeout=8):
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        d = s.read(512)
        buf += d
        if buf.endswith(b"> "):
            break
    return buf.decode(errors="replace")

s.write(b"\x03\r"); wait_prompt(3)                       # get a clean prompt
s.write(("put %s\r" % dest).encode()); time.sleep(0.3); s.read(512)
b64 = base64.b64encode(data).decode()
for i in range(0, len(b64), 76):
    s.write((b64[i:i + 76] + "\r").encode())
    time.sleep(0.01)
s.write(b".\r")
reply = wait_prompt(10).replace("\r", "").strip()
print(reply)
want = "put: wrote %d bytes, crc32 %08x" % (len(data), zlib.crc32(data) & 0xFFFFFFFF)
if want not in reply:
    sys.exit("transfer FAILED, expected: " + want)
print("transfer verified:", want)
if run:
    s.write(("run %s\r" % dest).encode())
    print(wait_prompt(5).replace("\r", "").strip())
