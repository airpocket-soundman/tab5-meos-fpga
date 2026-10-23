"""Run the actual Tab5 frame transaction engine against a fake DPI scanout."""
from pathlib import Path
import argparse,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='meos-frame-') as tmp:
    exe=Path(tmp)/'frame.exe'
    subprocess.run([a.cc,'cc','-O1','-UNDEBUG','-DM5OS_BOARD_TAB5=1','-I',str(root/'kernel/include'),str(root/'tests/console/display_frame_test.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
