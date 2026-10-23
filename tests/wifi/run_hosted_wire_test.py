from pathlib import Path
import argparse,subprocess,tempfile
r=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='meos-wifi-') as d:
 e=Path(d)/'wire-test.exe'
 subprocess.run([a.cc,'cc','-O1','-UNDEBUG','-I',str(r/'kernel/include'),str(r/'tests/wifi/hosted_wire_test.c'),'-o',str(e)],check=True)
 subprocess.run([str(e)],check=True)
