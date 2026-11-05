import argparse,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args();r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as t:
 exe=Path(t)/'entropy.exe'
 subprocess.run([a.cc,'cc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',str(r/'tests/ssh/entropy_clock_test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
