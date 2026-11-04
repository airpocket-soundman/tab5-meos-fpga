"""Compile actual console-focus helpers and UART device lease together."""
import argparse,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
r=Path(__file__).resolve().parents[1]
s=(r/'kernel/lib/kprintf.c').read_text(encoding='utf-8')
helpers=s[s.index('/* Input focus:'):s.index('\nvoid kputs(')]
model=(r/'tests/uart_focus_model.c').read_text(encoding='utf-8').replace('/* ACTUAL_FOCUS */',helpers)
with tempfile.TemporaryDirectory() as t:
    src=Path(t)/'focus.c';src.write_text(model,encoding='utf-8');exe=Path(t)/'focus.exe'
    subprocess.run([a.cc,'cc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',
        '-I'+str(r/'kernel/include'),'-I'+str(r),str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
