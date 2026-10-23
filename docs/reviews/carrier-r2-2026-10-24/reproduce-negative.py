from pathlib import Path
import pcbnew,json,os,subprocess,sys
root=Path(sys.argv[1]).resolve()
b=pcbnew.LoadBoard(str(root/'kicad/tab5-fpga-carrier.kicad_pcb'))
straps=[]
for fp in b.GetFootprints():
 if fp.GetReference() not in ('U4','U5'): continue
 pads={p.GetNumber():p for p in fp.Pads()}
 for a,c in [('1','10'),('2','9'),('4','7'),('5','6')]:
  p,q=pads[a].GetPosition(),pads[c].GetPosition()
  for t in list(b.GetTracks()):
   if isinstance(t,pcbnew.PCB_VIA):continue
   if ((t.GetStart()==p and t.GetEnd()==q) or (t.GetStart()==q and t.GetEnd()==p)):
    straps.append(t);break
for t in straps:b.Remove(t)
assert len(straps)==8,len(straps)
path=root/'without-straps.kicad_pcb';pcbnew.SaveBoard(str(path),b)
env=dict(os.environ,CHECK_PCB=str(path))
r=subprocess.run([sys.executable,'-X','utf8',str(root/'kicad/sim/check_continuity.py')],env=env,text=True,capture_output=True)
print(r.stdout)
(root/'audit_r2_negative.txt').write_text(r.stdout)
assert 'FAILED' in r.stdout

