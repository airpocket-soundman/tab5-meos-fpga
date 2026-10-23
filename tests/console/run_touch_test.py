"""Test the actual Tab5 touch reader with mocked I2C reports; no device access."""
from pathlib import Path
import argparse,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='meos-touch-') as tmp:
    exe=Path(tmp)/'touch.exe'
    subprocess.run([a.cc,'cc','-O1','-UNDEBUG','-DM5OS_BOARD_TAB5=1','-I',str(root/'kernel/include'),str(root/'tests/console/touch_test.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
