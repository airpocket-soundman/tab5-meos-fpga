"""Run the real console renderer with publication-boundary assertions."""
from pathlib import Path
import argparse,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='meos-render-') as tmp:
    exe=Path(tmp)/'render.exe'
    subprocess.run([a.cc,'cc','-O1','-UNDEBUG','-DM5OS_BOARD_TAB5=1','-I',str(root/'kernel/include'),str(root/'tests/console/render_test.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
