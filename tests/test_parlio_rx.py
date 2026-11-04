"""Compile actual finite RX helper against an ordering/failure model (no hardware)."""
import argparse,subprocess,tempfile
from pathlib import Path
r=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
with tempfile.TemporaryDirectory() as t:
 e=Path(t)/'rx.exe'
 subprocess.run([a.cc,'cc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-pointer-to-int-cast','-I'+str(r/'kernel/include'),str(r/'tests/parlio_rx_model.c'),'-o',str(e)],check=True)
 subprocess.run([str(e)],check=True)
