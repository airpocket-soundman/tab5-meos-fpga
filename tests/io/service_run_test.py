from pathlib import Path
import argparse,subprocess,tempfile
r=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='meos-io-service-') as t:
 e=Path(t)/'service.exe'
 subprocess.run([a.cc,'cc','-std=c11','-Wall','-Wextra','-Werror','-O1','-UNDEBUG','-DM5OS_BOARD_TAB5=1','-I',str(r/'kernel/include'),str(r/'tests/io/service_test.c'),'-o',str(e)],check=True)
 subprocess.run([str(e)],check=True)
