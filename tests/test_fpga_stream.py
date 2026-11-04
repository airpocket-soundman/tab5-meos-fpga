"""Source-aligned portable continuous queue/parser model. No hardware access."""
import argparse,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args();root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as t:
 exe=Path(t)/'stream.exe'
 subprocess.run([a.cc,'cc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror','-I'+str(root/'kernel/include'),str(root/'drivers/fpga/fpga_stream.c'),str(root/'tests/fpga_stream_fixture.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
