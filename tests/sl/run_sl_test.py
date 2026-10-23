from pathlib import Path
import argparse,json,hashlib,subprocess,tempfile
r=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
meta=json.loads((r/'third_party/sl/UPSTREAM.json').read_text())
for name,digest in meta['files'].items():
    assert hashlib.sha256((r/'third_party/sl'/name).read_bytes()).hexdigest()==digest,name
with tempfile.TemporaryDirectory(prefix='meos-sl-') as d:
    exe=Path(d)/'test.exe'
    subprocess.run([a.cc,'cc','-std=c11','-O1','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I',str(r/'kernel/include'),str(r/'tests/sl/sl_test.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
