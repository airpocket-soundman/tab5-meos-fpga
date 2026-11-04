"""Run actual standalone app against host device/API models. No hardware."""
import argparse,subprocess,tempfile,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args();root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as t:
 exe=Path(t)/'fractal.exe'
 subprocess.run([a.cc,'cc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror','-DM5OS_BOARD_TAB5=1','-I'+str(root/'kernel/include'),str(root/'apps/m5ex/fractal.c'),str(root/'tests/fractal_app_fixture.c'),'-o',str(exe)],check=True)
 for fault in range(10):
  run=subprocess.run([str(exe),str(fault)],check=True,capture_output=True,text=True)
  reports=[json.loads(s) for s in run.stdout.splitlines() if s.startswith('{')]
  if fault in (3,7,8,9):assert not reports
  else:
   assert reports and all(r['width']==32 and r['height']==32 for r in reports)
   assert reports[-1]['error'] == (0 if not fault else -1 if fault==2 else -5)
   if fault==2:assert reports[-1]['compare'] and reports[-1]['mismatch']==99
   if not fault:assert len(reports)==6 and sum(r['compare'] for r in reports)==1
  print(run.stdout.splitlines()[-1])
