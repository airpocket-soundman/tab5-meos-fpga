"""Actual GPU diagnostic C code against frozen independent carrier CPU images."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
BG=0x0841
def triangle(color,depth,coords,z,rgb):
    a,b,c=[(*coords[i:i+2],z) for i in (0,2,4)]
    def edge(u,v,x,y):return (v[0]-u[0])*(y-u[1])-(v[1]-u[1])*(x-u[0])
    area=edge(a,b,c[0],c[1])
    if area<0:b,c=c,b;area=-area
    for y in range(32):
        for x in range(32):
            pairs=((b,c),(c,a),(a,b));weights=[edge(u,v,16*x+8,16*y+8) for u,v in pairs]
            if not all(w>0 or w==0 and (v[1]<u[1] or v[1]==u[1] and v[0]>u[0]) for w,(u,v) in zip(weights,pairs)):continue
            value=sum(w*v[2] for w,v in zip(weights,(a,b,c)))//area;i=y*32+x
            if value>depth[i]:color[i]=rgb;depth[i]=value

def fractal(config,x,y):
    x0,y0,dx,dy,cx,cy,limit,julia=config;px=x0+x*dx;py=y0+y*dy
    zx,zy=(px,py) if julia else (0,0)
    if not julia:cx,cy=px,py
    it=0
    while zx*zx+zy*zy<67108864 and it<limit:
        zx,zy=(zx*zx-zy*zy)//4096+cx,2*zx*zy//4096+cy;it+=1
    if it==limit:return 0x0042
    n=it*3;r=32+n*9%224;g=60+n*5%196;b=100+n*3%156
    return ((r&248)<<8)|((g&252)<<3)|(b>>3)

def images():
    result=[];color=[BG]*1024;depth=[0]*1024
    result.append((color[:],depth[:]))
    first=[16,32,400,32,16,432];near=[112,16,496,304,48,480]
    triangle(color,depth,first,0x1234,0xf800);result.append((color[:],depth[:]))
    triangle(color,depth,first,0x1234,0x07e0);result.append((color[:],depth[:]))
    triangle(color,depth,near,0x2345,0x001f);result.append((color[:],depth[:]))
    for cfg in [(-10035,-4096,280,264,-2949,819,64,0),(-4096,-2048,132,132,-2949,819,64,1)]:
        result.append(([fractal(cfg,x,y) for y in range(32) for x in range(32)],[0]*1024))
    return result

if __name__=='__main__':
    if not __debug__:raise SystemExit('Assertions required')
    p=argparse.ArgumentParser();p.add_argument('--cc',required=True);p.add_argument('--output',type=Path);a=p.parse_args()
    meta=json.loads((ROOT/'tests/fixtures/fpga_tile_test_source.json').read_text())
    scenes=images();raw=b''
    for i,planes in enumerate(scenes):
        for j,words in enumerate(planes):
            data=struct.pack('<1024H',*words);assert hashlib.sha256(data).hexdigest()==meta['scene_hashes'][i][j],(i,j)
            raw+=data
    with tempfile.TemporaryDirectory(prefix='meos-tile-test-') as td:
        td=Path(td);fixture=td/'scenes.bin';fixture.write_bytes(raw);exe=td/'tile.exe'
        subprocess.run([a.cc,'cc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror','-Wframe-larger-than=512','-I'+str(ROOT/'kernel/include'),str(ROOT/'drivers/fpga/fpga_wire.c'),str(ROOT/'drivers/fpga/fpga_tile_test.c'),str(ROOT/'tests/fpga_tile_test_fixture.c'),'-o',str(exe)],check=True)
        subprocess.run([str(exe),str(fixture)],check=True)
    report={'carrier_revision':meta['carrier_revision'],'golden_scenes':6,'words_per_successful_run':12288,'tagged_and_untagged':True,'frame_limit_bytes':512,'hardware_access':False,'result':'pass'}
    print(json.dumps(report))
    if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n')
