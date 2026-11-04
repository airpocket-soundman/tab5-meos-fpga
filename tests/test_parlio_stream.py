"""Compile the actual native ring ISR against an independent completion model.

Tests ISR publication/closure/loss policy, not electrical timing or IRQ latency.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1]
s=(root/'hal/esp32p4/parlio_native_stream.inc').read_text(encoding='utf-8')
code=s[s.index('enum { STREAM_BLOCK'):s.index('static bool stream_phase_clock_ok')]
code+=s[s.index('static void stream_close_validation'):s.index('static void stream_clock_gpio_low')]
model=(root/'tests/parlio_stream_model.c').read_text(encoding='utf-8').replace('/* ACTUAL_ISR */',code)
with tempfile.TemporaryDirectory() as tmp:
    src=Path(tmp)/'model.c';src.write_text(model,encoding='utf-8');exe=Path(tmp)/'model.exe'
    subprocess.run([a.cc,'cc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
