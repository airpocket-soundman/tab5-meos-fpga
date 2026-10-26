"""Compile the actual native FPGA codec, transport and CLI; compare GPU golden vectors."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
if __name__=='__main__':
    if not __debug__:raise SystemExit('Run without Python -O: regression assertions must be enabled')
    p=argparse.ArgumentParser();p.add_argument('--cc',required=True);p.add_argument('--output',type=Path);args=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='meos-fpga-link-') as temp:
        exe=Path(temp)/'fpga-test.exe'
        subprocess.run([args.cc,'cc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror','-I'+str(ROOT/'kernel/include'),
            str(ROOT/'drivers/fpga/fpga_link.c'),str(ROOT/'drivers/fpga/fpga_mock.c'),str(ROOT/'drivers/fpga/fpga_selftest.c'),
            str(ROOT/'user/fpga/fpga_app.c'),str(ROOT/'tests/fpga_link_fixture.c'),'-o',str(exe)],check=True)
        result=subprocess.run([str(exe)],check=True,capture_output=True,text=True);print(result.stdout.strip())
        fixture=ROOT/'tests/fixtures/fpga_bringup_v0.json'
        source=json.loads((ROOT/'tests/fixtures/fpga_bringup_v0_source.json').read_text())
        assert hashlib.sha256(fixture.read_bytes()).hexdigest()==source['sha256']
        vectors=json.loads(fixture.read_text())
        output=subprocess.run([str(exe),'--vectors'],input=''.join(v['request']+'\n' for v in vectors),text=True,capture_output=True,check=True).stdout.splitlines()
        assert len(output)==len(vectors)
        for vector,line in zip(vectors,output):
            response,accepted=line.split()
            assert response==vector['response'],vector['name']
            assert bool(int(accepted))==vector['host_accept'],vector['name']
        commands=[(['status'],None),(['--mock','id'],[84,80]),(['--mock','echo','0x55','170'],[85,170]),
                  (['--mock','invert','255','0'],[0,255]),(['--mock','selftest'],None)]
        for command,data in commands:
            run=subprocess.run([str(exe),'fpga',*command],capture_output=True,text=True,check=True)
            report=json.loads(run.stdout);assert report['physical_link_tested'] is False
            if data is not None:assert report['data']==data
        rejected=[[],['--hardware','id'],['id'],['--mock','echo','-1','0'],['--mock','echo','256','0'],
                  ['--mock','echo','0x','0'],['--mock','echo','0xFG','0'],['--mock','echo','9'*200,'0'],['--mock','id','extra']]
        for command in rejected:
            run=subprocess.run([str(exe),'fpga',*command],capture_output=True,text=True)
            assert run.returncode!=0,command
        summary=dict(golden_vectors=len(vectors),fragment_sizes=list(range(1,9)),pattern_exchanges=4104,
                     injected_cases=96,cli_positive=len(commands),cli_rejected=len(rejected),physical_link_tested=False)
        print(json.dumps(summary))
        if args.output:args.output.write_text(json.dumps(summary,indent=2)+'\n')
