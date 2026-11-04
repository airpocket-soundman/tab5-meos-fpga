"""Run actual standalone app against host device/API models. No hardware."""
import argparse,subprocess,tempfile,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args();root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as t:
 exe=Path(t)/'fractal.exe'
 subprocess.run([a.cc,'cc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror','-DM5OS_BOARD_TAB5=1','-I'+str(root/'kernel/include'),str(root/'apps/m5ex/fractal.c'),str(root/'tests/fractal_app_fixture.c'),'-o',str(exe)],check=True)
 for fault in range(16):
  run=subprocess.run([str(exe),str(fault)],check=True,capture_output=True,text=True)
  reports=[json.loads(s) for s in run.stdout.splitlines() if s.startswith('{')]
  if fault in (3,7,8,9,13):assert not reports
  else:
   assert reports and all(r['width'] in (32,64,128,256) and r['height']==r['width'] for r in reports)
   assert reports[-1]['error'] == (0 if fault in (0,10,11,12,14,15) else -1 if fault==2 else -5)
   if fault==2:assert reports[-1]['compare'] and reports[-1]['mismatch']==((99//32)*128+99%32)
   if fault==10:
    assert len(reports)==10 and all(r['error']==0 for r in reports)
    assert all(reports[i]['phase_ms']<reports[i+1]['phase_ms'] for i in (0,1))
    assert not reports[3]['animation'] and reports[3]['phase_ms']==reports[4]['phase_ms']==reports[5]['phase_ms']
    assert reports[5]['compare'] and reports[5]['mismatch']==-1
    assert reports[-1]['animation'] and reports[-1]['phase_ms']>reports[5]['phase_ms']
    assert all(r['frame']==i+1 for i,r in enumerate(reports))
   if fault==11:
    assert [r['width'] for r in reports]==[128,128,128,128,256,32,64]
    assert all(r['compared_pixels']==r['width']**2 for r in reports[3:])
   if fault==12:assert len(reports)==1
   if fault==14:assert [r['width'] for r in reports]==[128,256]
   if fault==15:assert len(reports)==2 and reports[-1]['compared_pixels']==16384
   if not fault:assert len(reports)==11 and sum(r['compare'] for r in reports)==1
  print(run.stdout.splitlines()[-1])
