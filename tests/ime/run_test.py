from pathlib import Path
import argparse,subprocess,tempfile
r=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='meos-ime-') as d:
 exe=Path(d)/'ime.exe'
 sources=[r/'tests/ime/concurrency_test.c']+[r/'drivers/ime'/x for x in ['ime.c','conv.c','romaji.c','ime_dict.c','ime_mini_dict.c']]
 subprocess.run([a.cc,'cc','-O1','-UNDEBUG','-DM5OS_BOARD_TAB5=1','-I',str(r/'kernel/include'),*[str(s) for s in sources],'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
