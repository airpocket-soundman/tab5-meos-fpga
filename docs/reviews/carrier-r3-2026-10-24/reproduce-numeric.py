import sys,json,subprocess,hashlib
from pathlib import Path
import numpy as np
root=Path(sys.argv[1]).resolve();sim=root/'kicad/sim';sys.path.insert(0,str(sim))
import tmds_channel as tc,tmds_sparam as sp
tc.load();saved=json.loads((sim/'tmds_sparam.json').read_text());out={'revision':'91dc3b4','reproduction':{},'manifest_mismatches':[],'matrix':[]}
for tag,p,n in tc.PAIRS:
 M=sp.channel(tag,p,n);metrics=sp.metrics(M);S=sp.abcd_to_s4(M)
 out['reproduction'][tag]={'max_abs_error_db':max(abs(v-saved[tag][k]) for k,v in metrics.items()),'max_singular_value':float(np.linalg.svd(S,compute_uv=False).max()),'reciprocity_max_abs_error':float(abs(S-S.transpose(0,2,1)).max()),'metrics':metrics}
 for z in (85,100,115):
  for c in (.2,.35,.5):
   m=sp.metrics(sp.channel(tag,p,n,core_z=z,esd_cd=c*1e-12));out['matrix'].append({'pair':tag,'core_z':z,'esd_pf':c,**m})
# Exact invariant: matched uncoupled uniform lines have no reflection or cross-coupling.
M=sp.uncoupled(sp.F,50,.01,3);S=sp.abcd_to_s4(M)
out['matched_uniform_line']={'reflection_max':float(abs(S[:,:2,:2]).max()),'cross_transmission_max':float(max(abs(S[:,2,1]).max(),abs(S[:,3,0]).max()))}
manifest=json.loads((sim/'manifest_r3.json').read_text())
for name,rec in manifest['files'].items():
 data=subprocess.check_output(['git','-C',sys.argv[2],'show','91dc3b4:'+name])
 if name.endswith(('.py','.json','.csv','.kicad_pcb','.kicad_sch','.kicad_mod','.md','.html')):data=data.replace(b'\r\n',b'\n')
 actual=hashlib.sha256(data).hexdigest()
 if actual!=rec['sha256']:out['manifest_mismatches'].append({'file':name,'expected':rec['sha256'],'actual':actual})
Path(sys.argv[3]).write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps({k:v for k,v in out.items() if k!='matrix'},indent=2))
