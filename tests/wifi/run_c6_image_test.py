from pathlib import Path
import argparse,subprocess,tempfile
r=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);p.add_argument('--image',required=True);a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='meos-c6-') as d:
 e=Path(d)/'c6-image-test.exe'
 subprocess.run([a.cc,'cc','-O1','-UNDEBUG','-I',str(r/'kernel/include'),str(r/'tests/wifi/c6_image_test.c'),str(r/'kernel/lib/sha256.c'),'-o',str(e)],check=True)
 subprocess.run([str(e),a.image],check=True)
