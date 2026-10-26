"""Use the existing real BMP parser tests, then exercise the new viewer lifecycle."""
import argparse
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='meos-image-') as temp:
        for name,sources in [('bmp',[ROOT/'tests/camera/test_bmp.c']),('viewer',[ROOT/'tests/image_fixture.c',ROOT/'user/image/image_app.c'])]:
            exe=Path(temp)/(name+'.exe')
            subprocess.run([a.cc,'cc','-std=c11','-O1','-UNDEBUG','-DM5OS_BOARD_TAB5=1','-I'+str(ROOT/'kernel/include'),*map(str,sources),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
