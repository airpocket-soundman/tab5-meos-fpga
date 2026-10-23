from pathlib import Path
import argparse,subprocess,tempfile
r=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='meos-bluetooth-') as d:
 e=Path(d)/'bluetooth-test.exe'
 subprocess.run([a.cc,'cc','-std=c11','-Wall','-Wextra','-Werror','-O1','-UNDEBUG',str(r/'tests/bluetooth/bluetooth_test.c'),str(r/'drivers/bluetooth/hci_probe.c'),str(r/'drivers/bluetooth/hid_boot.c'),str(r/'drivers/bluetooth/hogp_session.c'),'-o',str(e)],check=True)
 subprocess.run([str(e)],check=True)
