#!/usr/bin/env python3
"""Drive the M5OS shell over serial.

usage: tools/shell_test.py PORT 'cmd1' 'cmd2' ...
Resets the board, waits for the first prompt, then sends each command and
prints everything the shell answers until the next prompt.
"""
import sys, time, serial

port = sys.argv[1]
cmds = sys.argv[2:]
s = serial.Serial(port, 115200, timeout=0.05)
import os, sys as _sys
_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from board_reset import reset_board
reset_board(s)

def read_until_prompt(timeout):
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        d = s.read(512)
        if d:
            buf += d
            if buf.endswith(b"> "):
                break
    return buf.decode(errors="replace")

boot = read_until_prompt(15)
print("=== boot tail ===")
print("\n".join(boot.replace("\r", "").split("\n")[-8:]))
for c in cmds:
    time.sleep(0.2)
    s.write(c.encode() + b"\r")
    out = read_until_prompt(8)
    print("=== $ %s ===" % c)
    print(out.replace("\r", "").rstrip())
