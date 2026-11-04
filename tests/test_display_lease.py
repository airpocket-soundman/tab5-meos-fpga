"""Actual display device lease tests; no simulated LCD correctness claim."""
import argparse,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as t:
    exe=Path(t)/'display-lease.exe'
    subprocess.run([a.cc,'cc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',
        '-DM5OS_BOARD_TAB5=1','-I'+str(r/'kernel/include'),str(r/'tests/display_lease_model.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
