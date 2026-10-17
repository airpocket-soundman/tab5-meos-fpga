#!/usr/bin/env python3
"""Tiny serial reader: tools/monitor.py [port] [seconds] [--send TEXT]"""
import sys, time, serial
port = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem1101"
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 5
send = sys.argv[sys.argv.index("--send") + 1] if "--send" in sys.argv else None
s = serial.Serial(port, 115200, timeout=0.1)
import os, sys as _sys
_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from board_reset import reset_board
reset_board(s)
end = time.time() + secs
sent = False
while time.time() < end:
    d = s.read(256)
    if d: sys.stdout.write(d.decode(errors="replace")); sys.stdout.flush()
    if send and not sent and time.time() > end - secs + 2:
        s.write(send.encode()); sent = True
