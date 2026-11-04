"""Max-size extended-TX diagnostics against independent generic triangle raster."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def images(triangle,count):
    color=[0x0841]*1024;depth=[0]*1024;scenes=[]
    for scene in range(3):
        for i in range(count):
            x=(i%8)*64;y=(i//8)*64;z=0x1000+i*257;rgb=(i*977)^0x5a3c
            if scene:
                rgb^=0xffff
                if scene==2:z+=-1 if i&1 else 1024
            triangle(color,depth,[x,y,x+64,y,x,y+64],z,rgb)
        scenes.append((color[:],depth[:]))
    return scenes

if __name__=='__main__':
    if not __debug__:raise SystemExit('Assertions required')
    p=argparse.ArgumentParser();p.add_argument('--cc',required=True);p.add_argument('--output',type=Path);a=p.parse_args()
    spec=importlib.util.spec_from_file_location('tile_oracle',ROOT/'tests/test_fpga_tile_test.py');oracle=importlib.util.module_from_spec(spec);spec.loader.exec_module(oracle)
    meta=json.loads((ROOT/'tests/fixtures/fpga_batch_test_source.json').read_text())
    raw=b''
    for count in range(1,65):
        for scene,planes in enumerate(images(oracle.triangle,count)):
            for plane,words in enumerate(planes):
                data=struct.pack('<1024H',*words)
                assert hashlib.sha256(data).hexdigest()==meta['hashes'][count-1][scene][plane],(count,scene,plane)
                raw+=data
    with tempfile.TemporaryDirectory(prefix='meos-batch-test-') as td:
        td=Path(td);fixture=td/'scenes.bin';fixture.write_bytes(raw);exe=td/'batch.exe'
        subprocess.run([a.cc,'cc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror','-Wframe-larger-than=512','-I'+str(ROOT/'kernel/include'),str(ROOT/'drivers/fpga/fpga_wire.c'),str(ROOT/'drivers/fpga/fpga_tile_test.c'),str(ROOT/'tests/fpga_batch_test_fixture.c'),'-o',str(exe)],check=True)
        subprocess.run([str(exe),str(fixture)],check=True)
    report={'carrier_revision':meta['carrier_revision'],'counts':list(range(1,65)),'max_request_bytes':1290,'batches_per_successful_run':3,'words_per_successful_run':6144,'tagged_and_untagged':True,'frame_limit_bytes':512,'hardware_access':False,'result':'pass'}
    print(json.dumps(report))
    if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n')
