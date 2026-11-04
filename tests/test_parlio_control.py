"""Exercise actual P4 short-control functions with an independent pin trace model.

Checks logic and nominal wait bounds, not physical edge speed or IRQ latency.
"""
import argparse, subprocess, tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1]
s=(root/'hal/esp32p4/parlio_native.c').read_text(encoding='utf-8')
code=s[s.index('int hal_parlio_short_tx('):s.index('int hal_parlio_tx(')]
model=(root/'tests/parlio_control_model.c').read_text().replace('/* ACTUAL_CONTROL */',code)
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp);src=tmp/'model.c';src.write_text(model);exe=tmp/'model.exe'
    subprocess.run([a.cc,'cc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',
        '-I'+str(root),'-I'+str(root/'kernel/include'),str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
