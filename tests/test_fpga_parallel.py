import argparse,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as t:
 e=Path(t)/'parallel.exe'
 subprocess.run([a.cc,'cc','-std=c11','-O2','-UNDEBUG','-DM5OS_FPGA_DOCK_VERIFIED=1','-Wall','-Wextra','-Werror','-I'+str(r/'kernel/include'),*[str(r/s) for s in ['drivers/fpga/fpga_link.c','drivers/fpga/fpga_wire.c','drivers/fpga/fpga_parallel.c','tests/fpga_parallel_fixture.c']],'-o',str(e)],check=True)
 subprocess.run([str(e)],check=True)
