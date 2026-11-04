"""Reusable GPU surface API with independent golden geometry and mock display."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
if __name__=='__main__':
    if not __debug__:raise SystemExit('Assertions required')
    p=argparse.ArgumentParser();p.add_argument('--cc',required=True);p.add_argument('--output',type=Path);a=p.parse_args()
    spec=importlib.util.spec_from_file_location('tile_oracle',ROOT/'tests/test_fpga_tile_test.py');tile=importlib.util.module_from_spec(spec);spec.loader.exec_module(tile)
    spec=importlib.util.spec_from_file_location('batch_oracle',ROOT/'tests/test_fpga_batch_test.py');batch=importlib.util.module_from_spec(spec);spec.loader.exec_module(batch)
    words=batch.images(tile.triangle,64)[0][0];data=struct.pack('<1024H',*words)
    meta=json.loads((ROOT/'tests/fixtures/fpga_batch_test_source.json').read_text());assert hashlib.sha256(data).hexdigest()==meta['hashes'][63][0][0]
    with tempfile.TemporaryDirectory(prefix='meos-surface-') as td:
        td=Path(td);fixture=td/'image.bin';fixture.write_bytes(data);exe=td/'surface.exe'
        subprocess.run([a.cc,'cc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror','-Wframe-larger-than=512','-DM5OS_BOARD_TAB5=1','-I'+str(ROOT/'kernel/include'),str(ROOT/'drivers/fpga/fpga_wire.c'),str(ROOT/'drivers/fpga/fpga_surface.c'),str(ROOT/'tests/fpga_surface_fixture.c'),'-o',str(exe)],check=True)
        subprocess.run([str(exe),str(fixture)],check=True)
    report={'carrier_revision':meta['carrier_revision'],'actual_readback_pixels':1024,'max_triangle_batch':64,'max_request_bytes':1290,'tagged_and_untagged':True,'display_failure_and_bounds':True,'poison_and_no_replay':True,'frame_limit_bytes':512,'hardware_access':False,'result':'pass'}
    print(json.dumps(report))
    if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n')
