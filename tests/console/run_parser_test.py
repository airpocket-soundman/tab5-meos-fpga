"""Compile actual native and SSH output parsers into host regression harnesses."""
from pathlib import Path
import argparse, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='meos-term-') as tmp:
    tmp=Path(tmp)
    exe=tmp/'native.exe'
    subprocess.run([a.cc,'cc','-O1','-UNDEBUG','-DM5OS_BOARD_TAB5=1','-I',str(root/'kernel/include'),str(root/'tests/console/parser_test.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
    source=(root/'user/ssh/ssh_app.c').read_text()
    start=source.index('typedef struct {\n    uint16_t ch;')
    end=source.index('static void tab_feed_str(',start)
    harness=(root/'tests/console/ssh_parser_test.c').read_text()
    harness=harness.replace('/* ACTUAL_SSH_PARSER */',source[start:end])
    test=tmp/'ssh.c';test.write_text(harness)
    exe=tmp/'ssh.exe'
    subprocess.run([a.cc,'cc','-O1','-UNDEBUG','-I',str(root/'user/ssh'),str(test),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
