from pathlib import Path
import argparse, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='meos-uart-') as d:
    for name in ('uart_test','uart_mmio_test'):
        exe=Path(d)/(name+'.exe')
        subprocess.run([a.cc,'cc','-std=c11','-Wall','-Wextra','-Werror','-O1','-UNDEBUG','-I',str(root/'tests/io/uart_mmio'),'-I',str(root/'kernel/include'),'-I',str(root/'tests/io'),str(root/('tests/io/'+name+'.c')),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
