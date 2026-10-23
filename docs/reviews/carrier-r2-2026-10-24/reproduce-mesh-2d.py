from pathlib import Path
import sys,json,time
sys.path.insert(0,str(Path(sys.argv[1]).resolve()/'kicad/sim'))
import microstrip2d as m
out=[]
for dx in (.02104,.01052,.007013333333):
 t=time.time(); r=m.pair(.2,.13,dx=dx); out.append({'dx_mm':dx,**r,'seconds':time.time()-t}); print(out[-1],flush=True)
Path(sys.argv[2]).write_text(json.dumps(out,indent=2))
