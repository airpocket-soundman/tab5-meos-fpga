"""Test real GPIO backend against an event-level IO model, never real pins."""
import argparse,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
if not __debug__:raise SystemExit('Assertions must be enabled')
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as t:
 for verified in (0,1):
  exe=Path(t)/('gpio'+str(verified)+'.exe')
  subprocess.run([a.cc,'cc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-unused-function','-DM5OS_FPGA_DOCK_VERIFIED='+str(verified),'-I'+str(r/'kernel/include'),str(r/'drivers/fpga/fpga_link.c'),str(r/'drivers/fpga/fpga_gpio.c'),str(r/'tests/fpga_gpio_fixture.c'),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
