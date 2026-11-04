"""Compile actual portable wire engine; validate independent Python wire vectors."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def crc(data,seed=0xffff):
    value=seed
    for byte in data:
        value^=byte<<8
        for _ in range(8):value=((value<<1)^0x1021 if value&0x8000 else value<<1)&0xffff
    return value

def seal(data):return data+crc(data).to_bytes(2,'little')
def request(op,a=0,b=0,seq=37):return seal(bytes([0xa5,op,seq,2,a,b]))
def response(q):
    op=q[1]&127
    args=b'TP' if op==1 else bytes([q[4],q[5]])
    header=seal(bytes([0x5a,0,q[2],2])+args)
    size=74 if op==0x2f else 2058 if op==0x3b else 8
    if size==8:return header
    payload=bytes((i*37+q[4]*11+q[5]*19)&255 for i in range(size-10))
    return header+payload+crc(header[:6]+payload).to_bytes(2,'little')
def fnv(data):
    h=2166136261
    for b in data:h=((h^b)*16777619)&0xffffffff
    return h

if __name__=='__main__':
    if not __debug__:raise SystemExit('Run with assertions enabled')
    p=argparse.ArgumentParser();p.add_argument('--cc',required=True);p.add_argument('--output',type=Path);a=p.parse_args()
    manifest=json.loads((ROOT/'third_party/carrier_link/source.json').read_text())
    for name,entry in manifest['files'].items():assert hashlib.sha256((ROOT/'third_party/carrier_link'/name).read_bytes()).hexdigest()==entry['sha256']
    cases=[]
    def add(q,raw,size,good,offset=0):cases.append((q,raw,size,good,offset))
    for op,aa,bb in [(1,0,0),(2,0x55,0xaa),(0x2f,31,0),(0x2f,0,1),(0x3b,0,0),(0x3b,0,1)]:
        q=request(op,aa,bb);r=response(q);n=len(r);add(q,r,n,True)
        for i in range(n):
            bad=bytearray(r);bad[i]^=1;add(q,bytes(bad),n,False)
        for trim in [1,2,7,n-1]:add(q,r[:-trim],n,False)
        add(q,r+b'\0',n,False)
        for margin in [8,16]:
            tagged=request(op|128,aa,bb);r=response(tagged)
            for offset in range(margin+1):
                raw=b'\xc3'*offset+r+b'\xe7'*(margin-offset)
                add(tagged,raw,n,True,offset)
            add(tagged,b'\xc3'*margin+r[:-1]+b'\xe7',n,False)
            add(tagged,b'\xc3'*margin+r[:-1],n,False)
            bad=bytearray(r);bad[2]^=1;bad[:8]=seal(bytes(bad[:6]));add(tagged,b'\xc3'*margin+bytes(bad),n,False)
            if n==8:add(tagged,r+r+b'\xe7'*(margin-8),n,False)
    q=request(0x81);r=response(q);add(q,r,8,False);add(request(0x81,seq=38),b'\xc3'*8+r,8,False)
    badq=bytearray(q);badq[7]^=1;add(bytes(badq),b'\xc3'*8+r,8,False)
    with tempfile.TemporaryDirectory(prefix='meos-wire-') as t:
        exe=Path(t)/'wire.exe'
        subprocess.run([a.cc,'cc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror','-I'+str(ROOT/'kernel/include'),str(ROOT/'drivers/fpga/fpga_wire.c'),str(ROOT/'tests/fpga_wire_fixture.c'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
        payload=''.join(f'{q.hex()} {raw.hex()} {size}\n' for q,raw,size,_,_ in cases)
        lines=subprocess.check_output([str(exe),'--validate'],input=payload,text=True).splitlines();assert len(lines)==len(cases)
        for case,line in zip(cases,lines):
            q,raw,size,good,offset=case;rc,at,h=map(int,line.split())
            assert (rc==0)==good,(q.hex(),size,good,rc)
            if good:assert at==offset and h==fnv(raw[offset:offset+size]),(q.hex(),at,offset)
        result={'carrier_revision':manifest['revision'],'independent_validation_vectors':len(cases),'source_hashes_verified':len(manifest['files']),'hardware_access':False,'result':'pass'}
        print(json.dumps(result))
        if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n')
