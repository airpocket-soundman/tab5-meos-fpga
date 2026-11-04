import argparse,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
r=Path(__file__).resolve().parents[1]
for flags,expected in (([],0),(['M5OS_MODULE_FPGA_PARLIO=1'],8),(['M5OS_MODULE_CAMERA=1'],16),(['M5OS_USB_NATIVE_DMA=1'],32)):
 with tempfile.TemporaryDirectory() as t:
  exe=Path(t)/'suspend.exe'
  subprocess.run([a.cc,'cc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror','-DM5OS_BOARD_TAB5=1','-DEXPECTED_FIXED='+str(expected),*['-D'+f for f in flags],'-I'+str(r/'kernel/include'),str(r/'tests/suspend_safety_fixture.c'),str(r/'kernel/lib/suspend_core.c'),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
