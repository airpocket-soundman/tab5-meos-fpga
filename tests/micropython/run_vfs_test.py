"""Production VFS adapter fault tests with a model of MicroPython C APIs, not a VM."""
from pathlib import Path
import argparse,subprocess,tempfile
r=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='meos-mp-vfs-') as d:
 exe=Path(d)/'vfs.exe'
 subprocess.run([a.cc,'cc','-target','x86-windows-gnu','-std=c11','-O1','-UNDEBUG','-Wall','-Wextra','-Wno-unused-function','-Wno-unused-const-variable','-I',str(r/'tests/micropython/vfs_stub'),'-I',str(r/'kernel/include'),str(r/'tests/micropython/vfs_test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
