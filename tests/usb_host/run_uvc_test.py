from pathlib import Path
import argparse,subprocess,tempfile
r=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='meos-uvc-') as d:
 e=Path(d)/'uvc-test.exe'
 subprocess.run([a.cc,'cc','-O1','-UNDEBUG','-I',str(r/'kernel/include'),str(r/'tests/usb_host/uvc_test.c'),'-o',str(e)],check=True)
 subprocess.run([str(e),str(r/'tests/usb_host/fixtures/elecom_056e_700a.bin')],check=True)
