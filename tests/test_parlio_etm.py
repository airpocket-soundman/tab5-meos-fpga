"""Compile actual native ETM helper code against an event/register model."""
import argparse,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
r=Path(__file__).resolve().parents[1]
s=(r/'hal/esp32p4/parlio_native.c').read_text()
helpers=s[s.index('static unsigned gate_event_id'):s.index('static bool pair_unused')]
model=(r/'tests/parlio_etm_model.c').read_text().replace('/* ACTUAL_HELPERS */',helpers)
with tempfile.TemporaryDirectory() as t:
 src=Path(t)/'model.c';src.write_text(model);exe=Path(t)/'model.exe'
 subprocess.run([a.cc,'cc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
