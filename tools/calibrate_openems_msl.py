"""Straight lossless microstrip calibration, adapted from openEMS MSL_NotchFilter tutorial.
Zero-thickness PEC, er4.4, h0.2104mm, width0.3mm; modal MSL ports with measurement planes inside a uniform line.
"""
import os,sys,json,time
from pathlib import Path
import numpy as np
import argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--package',required=True,type=Path)
parser.add_argument('--work-dir',required=True,type=Path)
parser.add_argument('--mesh',type=float,default=.04)
parser.add_argument('--z-divisions',type=int,default=4)
parser.add_argument('--postprocess-only',action='store_true')
args=parser.parse_args()
if args.mesh <= 0 or args.z_divisions < 2: parser.error('positive mesh and at least two substrate divisions required')
pkg=str(args.package.resolve())
os.environ['CSXCAD_INSTALL_PATH']=pkg
os.environ['OPENEMS_INSTALL_PATH']=pkg
_dll=os.add_dll_directory(pkg)
from CSXCAD import ContinuousStructure
from openEMS import openEMS
wd=args.work_dir.resolve(); fine=args.mesh
fd=openEMS(NrTS=60000,EndCriteria=1e-5)
fd.SetGaussExcite(5e9,5e9)
fd.SetBoundaryCond(['PML_8','PML_8','MUR','MUR','PEC','MUR'])
cs=ContinuousStructure();fd.SetCSX(cs);mesh=cs.GetGrid();mesh.SetDeltaUnit(1e-3)
h=.2104;w=.3
mesh.AddLine('x',[-15,-10,-5,0,5,10,15]);mesh.SmoothMeshLines('x',.25,1.4)
mesh.AddLine('y',[0,w/2-fine/3,w/2+2*fine/3,-w/2+fine/3,-w/2-2*fine/3]);mesh.SmoothMeshLines('y',fine,1.4)
mesh.AddLine('y',[-3,3]);mesh.SmoothMeshLines('y',.25,1.4)
mesh.AddLine('z',np.linspace(0,h,args.z_divisions+1));mesh.AddLine('z',[h+.1,h+.3,2.5]);mesh.SmoothMeshLines('z',.25,1.4)
sub=cs.AddMaterial('substrate',epsilon=4.4);sub.AddBox([-15,-3,0],[15,3,h])
pec=cs.AddMetal('trace')
ports=[fd.AddMSLPort(1,pec,[-15,-w/2,h],[0,w/2,0],'x','z',excite=-1,FeedShift=2,MeasPlaneShift=7,priority=10),fd.AddMSLPort(2,pec,[15,-w/2,h],[0,w/2,0],'x','z',MeasPlaneShift=7,priority=10)]
wd.mkdir(parents=True,exist_ok=True)
if args.postprocess_only:
    previous=json.loads((wd/'result.json').read_text())
    if previous['lateral_mesh_mm'] != fine or previous.get('substrate_z_divisions',4) != args.z_divisions:
        parser.error('postprocessing parameters must match the saved model')
else:
    if (wd/'model.xml').exists(): parser.error('use a new work directory for each simulation')
    cs.Write2XML(str(wd/'model.xml'))
t=time.time()
if not args.postprocess_only: fd.Run(str(wd),cleanup=True,numThreads=4)
f=np.linspace(.5e9,2e9,61)
for p in ports:p.CalcPort(str(wd),f,ref_impedance=50)
a=ports[0].uf_ref/ports[0].uf_inc;b=ports[1].uf_ref/ports[0].uf_inc
z=50*np.sqrt(((1+a)**2-b*b)/((1-a)**2-b*b))
out={'model':'straight zero-thickness lossless microstrip, modal MSL ports','width_mm':w,'h_mm':h,'er':4.4,'lateral_mesh_mm':fine,'substrate_z_divisions':args.z_divisions,'postprocess_only':args.postprocess_only,'elapsed_this_invocation_s':time.time()-t,'z_mean_ohm':float(z.real.mean()),'z_min_ohm':float(z.real.min()),'z_max_ohm':float(z.real.max()),'s21_db_mean':float(np.mean(20*np.log10(abs(b)))),'frequencies_hz':f.tolist(),'z_real_ohm':z.real.tolist()}
(wd/'result.json').write_text(json.dumps(out,indent=2));print(json.dumps({k:v for k,v in out.items() if not isinstance(v,list)}))
