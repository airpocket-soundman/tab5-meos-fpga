from pathlib import Path
import pcbnew,sys,subprocess,os,json
root=Path(sys.argv[1]).resolve();pcb=root/'kicad/tab5-fpga-carrier.kicad_pcb'
out={}
b=pcbnew.LoadBoard(str(pcb));fp=next(f for f in b.GetFootprints() if f.GetReference()=='U4');p=next(p for p in fp.Pads() if p.GetNumber()=='3');p.SetSize(pcbnew.VECTOR2I(pcbnew.FromMM(.1),pcbnew.FromMM(.1)))
path=root/'mutated-u4-ground-land.kicad_pcb';pcbnew.SaveBoard(str(path),b)
env=dict(os.environ,CHECK_PCB=str(path));r=subprocess.run([sys.executable,'-X','utf8',str(root/'kicad/sim/check_esd_footprint.py')],env=env,capture_output=True,text=True)
out['placed_ground_pad_shrunk_to_0_1mm']={'exit_code':r.returncode,'unexpected_pass':r.returncode==0,'output':r.stdout}
b=pcbnew.LoadBoard(str(pcb));t=next(t for t in b.GetTracks() if not isinstance(t,pcbnew.PCB_VIA) and t.GetNetname()=='HDMI_D0P' and abs(pcbnew.ToMM(t.GetWidth())-.2)<.001);t.SetWidth(pcbnew.FromMM(.18))
path=root/'mutated-trace-width.kicad_pcb';pcbnew.SaveBoard(str(path),b)
env=dict(os.environ,CHECK_PCB=str(path));r=subprocess.run([sys.executable,'-X','utf8',str(root/'kicad/sim/check_classification.py')],env=env,capture_output=True,text=True)
out['trace_width_0_2_to_0_18mm']={'exit_code':r.returncode,'unexpected_pass':r.returncode==0,'output':r.stdout}
# Independent placed-pad comparison against library, for the actual target PCB.
b=pcbnew.LoadBoard(str(pcb));lib=pcbnew.FootprintLoad(str(root/'kicad/lib/carrier.pretty'),'DFN2510-10L_RCLAMP0524P');expected={p.GetNumber():p for p in lib.Pads()}
out['actual_placed_pads_match_library']={}
for fp in b.GetFootprints():
 if fp.GetReference() not in ('U4','U5'):continue
 differences=[]
 for p in fp.Pads():
  q=expected[p.GetNumber()]
  if p.GetSize()!=q.GetSize():differences.append(p.GetNumber())
 out['actual_placed_pads_match_library'][fp.GetReference()]={'size_mismatches':differences}
Path(sys.argv[2]).write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps({k: {a:b for a,b in v.items() if a!='output'} for k,v in out.items()},indent=2))
