from pathlib import Path
import argparse,subprocess,tempfile
r=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='meos-sftp-local-test-') as d:
    exe=Path(d)/'test.exe'
    subprocess.run([a.cc,'cc','-target','x86-windows-gnu','-O1','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I',str(r/'kernel/include'),str(r/'tests/sftp/storage_selftest_test.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
