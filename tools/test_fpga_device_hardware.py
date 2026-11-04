"""Run the fixed fpgabench M5EX already uploaded to /sd/fpgabench.m5ex.

No flashing, FPGA programming, timing selection, retries or GPIO access.
Requires an authorized physical Dock setup and matching firmware/FPGA image.
"""
import argparse
import hashlib
import json
import time
from pathlib import Path
import serial

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--port', required=True)
p.add_argument('--app', type=Path, required=True)
p.add_argument('--firmware', type=Path, required=True)
p.add_argument('--record', type=Path, required=True)
p.add_argument('--runs', type=int, default=1, choices=range(1,101))
a = p.parse_args()
record = {'port': a.port, 'app_sha256': hashlib.sha256(a.app.read_bytes()).hexdigest(),
          'firmware_sha256': hashlib.sha256(a.firmware.read_bytes()).hexdigest(),
          'checks': [], 'runs': a.runs, 'scope': '96 fixed 32x32 device calls per run; no LCD timing'}
s = serial.Serial()
s.port, s.baudrate, s.timeout = a.port, 115200, .01
# Avoid toggling the P4 reset/download lines when opening the console.
s.dtr = s.rts = False
s.open()

def save():
    a.record.parent.mkdir(parents=True, exist_ok=True)
    a.record.write_text(json.dumps(record, indent=2), encoding='utf-8')

def drain(seconds):
    end, data = time.monotonic()+seconds, b''
    while time.monotonic() < end:
        data += s.read(4096)
    return data.decode(errors='replace')

def command(text):
    for ch in (text+'\r').encode():
        s.write(bytes([ch]))
        time.sleep(.04)

try:
    drain(3)
    s.write(b'\x03\r')
    drain(.5)
    for run in range(a.runs):
        command('run /sd/fpgabench.m5ex')
        pending, raw, reports = '', '', []
        end = time.monotonic()+45
        while time.monotonic() < end and len(reports) < 6:
            chunk = s.read(4096).decode(errors='replace')
            pending += chunk
            raw += chunk
            while '\n' in pending:
                line, pending = pending.split('\n', 1)
                at = line.find('{')
                if at < 0:
                    continue
                try:
                    r = json.loads(line[at:])
                except ValueError:
                    continue
                if not r.get('fpgabench'):
                    continue
                reports.append(r)
                record['checks'].append(dict(r, run=run))
                record['output'] = record.get('completed_output','')+raw
                save()
                assert r['error'] == 0 and r['calls'] == 16 and r['matched'] == 16384, r
                print(r, flush=True)
        assert [(r['scene'], r['repeat']) for r in reports] == [
            (scene, repeat) for scene in ('mandelbrot', 'julia') for repeat in range(3)]
        record['output'] = record.get('completed_output','')+raw
        record['completed_output'] = record['output']
        drain(.3)
    record.pop('completed_output',None)
    record['result'] = 'passed'
except Exception as e:
    record['result'], record['error'] = 'failed', str(e)
    raise
finally:
    save()
    s.close()
