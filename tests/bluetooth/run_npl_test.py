from pathlib import Path
import argparse,subprocess,tempfile
r=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--cc',required=True)
p.add_argument('--nimble',required=True,help='upstream nimble/include directory containing nimble/nimble_npl.h')
a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='meos-npl-') as d:
 e=Path(d)/'npl-test.exe'
 subprocess.run([a.cc,'cc','-std=c11','-Wall','-Wextra','-Werror','-DNIMBLE_CFG_CONTROLLER=0','-O1','-UNDEBUG','-I',str(r/'drivers/bluetooth/npl'),'-I',a.nimble,'-I',str(r/'kernel/include'),str(r/'tests/bluetooth/npl_test.c'),'-o',str(e)],check=True)
 subprocess.run([str(e)],check=True)
