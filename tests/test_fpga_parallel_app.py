"""Host-only CLI/resource/display ordering model; no physical claims."""
import argparse,json,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 exe=Path(tmp)/'app.exe'
 subprocess.run([a.cc,'cc','-std=c11','-Wall','-Wextra','-Werror','-DM5OS_MODULE_FPGA_GFX=0','-DM5OS_BOARD_TAB5=1','-I'+str(root/'kernel/include'),str(root/'user/fpga/fpga_parallel_app.c'),str(root/'drivers/fpga/fpga_link.c'),str(root/'tests/fpga_parallel_app_fixture.c'),'-o',str(exe)],check=True)
 def run(cmd='id',opts=(),fault=None):
  r=subprocess.run([str(exe),*([f'fault{fault}'] if fault else []),'--parlio','dock-jumper-v0','1000000',cmd,*opts],text=True,capture_output=True)
  return r,[json.loads(s) for s in r.stdout.splitlines() if s.startswith('{')]
 for hz in (500000,1000000,2000000,5000000,8000000,10000000,16000000,20000000):
  r,j=run(opts=['--rx-hz',str(hz)]);assert not r.returncode and j[0]['hz']==1000000 and j[0]['rx_hz']==hz
  assert j[0]['rx']=='bulk-continuous+short-gpio' and j[1]['stream']
  assert j[0]['drive_cap_zero_verified'] is True
 for mode,label in (('--finite-rx','finite-dma'),('--gpio-rx','gpio')):
  r,j=run(opts=[mode]);assert not r.returncode and j[0]['rx']==label and len(j)==1
 for opts in (['--rx-hz'],['--rx-hz','4000000'],['--rx-hz','20000001'],['--rx-hz','-1'],['--rx-hz','1000000','--gpio-rx'],['--gpio-rx','--gpio-rx'],['--finite-rx','--finite-rx'],['--finite-rx','--gpio-rx'],['--gpio-rx','--finite-rx'],['--rx-hz','1000000','--rx-hz','2000000']):
  r,j=run(opts=opts);assert r.returncode and not j
 for fault in (None,1,2,3):
  r,j=run('tile-demo',fault=fault);assert bool(r.returncode)==bool(fault)
  tile=next(x for x in j if x.get('gpu_tile_test'))
  assert j[0]['result']==('io' if fault else 'ok') and tile['result']==j[0]['result']
  assert j[0]['lcd_submitted']==(not fault) and j[0]['reset_required']==(fault==3)
 r,j=run(fault=4);assert r.returncode and len(j)==3
 j=[x for x in j if not x.get('stream')]
 assert j[1]['wire_first_failure'] and j[1]['result']=='data' and j[1]['received_length']==24
 assert len(j[1]['raw'])==24 and j[1]['raw'][:3]==[195,195,90] and j[1]['request_header']==[165,130,1,2,0,0,0,0]
 assert not j[1]['truncated']
 print('Parallel app: RX rate parsing, display failure authority, close failure and cleanup passed')
