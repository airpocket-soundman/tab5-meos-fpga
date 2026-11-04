"""Actual viewer/shared BMP host regression; no device access."""
import argparse,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args();r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as t:
 exe=Path(t)/'viewer.exe'
 subprocess.run([a.cc,'cc','-std=c11','-O1','-g','-UNDEBUG','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-DM5OS_BOARD_TAB5=1','-I'+str(r/'kernel/include'),str(r/'tests/image_merge_fixture.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
