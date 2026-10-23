import sys,json,time,types,math
from pathlib import Path
root=Path(sys.argv[1]).resolve()/'kicad/sim';sys.path.insert(0,str(root))
import microstrip2d as m
src=(root/'microstrip2d.py').read_text()
a=src.index('    # energy =');b=src.index('    return w',a)
src=src[:a]+'''    w = 0.5 * EPS0 * (np.sum(ex * np.diff(phi,axis=0)**2) + np.sum(ey * np.diff(phi,axis=1)**2))
'''+src[b:]
fixed=types.ModuleType('energy_checked');exec(compile(src,'energy_checked','exec'),fixed.__dict__)
rows=[]
for dx in (.02104,.01052):
 for label,solver in [('original',m),('discrete_link',fixed)]:
  t=time.time();r=solver.pair(.2,.13,dx=dx);single=solver.single(.3,dx=dx);row={'energy':label,'dx_mm':dx,'zdiff_ohm':float(r['zdiff']),'single_ohm':float(single[0]),'seconds':time.time()-t};rows.append(row);print(row,flush=True)
# A zero-thickness variant is needed for the modal 3D reference (different from actual 35um copper).
thin=types.ModuleType('thin');exec(compile(src.replace('nt = max(int(round(t / dx)), 1)','nt = max(int(round(t / dx)), 0)'),'thin','exec'),thin.__dict__)
rows.append({'model':'zero_thickness_reference','single_ohm':float(thin.single(.3,t=0,dx=.01052)[0])})
Path(sys.argv[2]).write_text(json.dumps(rows,indent=2));print(rows[-1],flush=True)
