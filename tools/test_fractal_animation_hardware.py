"""Measure the installed fractal viewer including presentation, at one frozen pose.

Requires prior authorization/setup. No firmware writes, FPGA programming or
transport retries. Leaves the app closed after verifying cleanup statistics.
"""
import argparse, hashlib, json, statistics, time
from pathlib import Path
import serial
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--port',required=True)
p.add_argument('--app',type=Path,required=True)
p.add_argument('--firmware',type=Path,required=True)
p.add_argument('--record',type=Path,required=True)
a=p.parse_args()
record={'app_sha256':hashlib.sha256(a.app.read_bytes()).hexdigest(),
        'firmware_sha256':hashlib.sha256(a.firmware.read_bytes()).hexdigest(),
        'checks':[],'visual_inspection':False}
s=serial.Serial();s.port=a.port;s.baudrate=115200;s.timeout=.01;s.dtr=s.rts=False;s.open()
def save():a.record.write_text(json.dumps(record,indent=2),encoding='utf-8')
def drain(t):
    end=time.monotonic()+t;data=b''
    while time.monotonic()<end:data+=s.read(4096)
    return data.decode(errors='replace')
def command(cmd):
    for c in (cmd+'\r').encode():s.write(bytes([c]));time.sleep(.04)
    return drain(.4)
def frame(label,key,match,count=1):
    drain(.05)
    if key:s.write(key.encode());s.flush()
    end=time.monotonic()+15;pending='';raw='';reports=[]
    while time.monotonic()<end:
        chunk=s.read(4096).decode(errors='replace');raw+=chunk;pending+=chunk
        while '\n' in pending:
            line,pending=pending.split('\n',1);at=line.find('{')
            if at<0:continue
            try:r=json.loads(line[at:])
            except ValueError:continue
            if not r.get('fractal'):continue
            assert r['error']==0,r
            if match(r):reports.append(r)
            if len(reports)>=count:
                record['checks'].append({'label':label,'key':key,'reports':reports,'output':raw});save()
                print(label,[(r['engine'],r['width'],r['frame_ms'],r['fps_milli'])for r in reports],flush=True)
                return reports[-1]
    record['checks'].append({'label':label,'key':key,'reports':reports,'output':raw});save()
    raise RuntimeError('No matching frame: '+label)
try:
    drain(3);s.write(b'\x03\r');drain(.5)
    record['memory_before']=command('mem --json');command('run /sd/fractal.m5ex')
    frame('CPU live',None,lambda r:r['engine']=='cpu' and r['animation'],count=10)
    frame('freeze pose','0',lambda r:r['phase_ms']==0 and not r['animation'])
    for scene,key in [('mandelbrot','M'),('julia','J')]:
        frame(scene,key,lambda r,n=scene:r['scene']==n)
        for backend,key in [('cpu','C'),('fpga','G')]:
            for repeat in range(5):
                frame(scene+' '+backend+' '+str(repeat),key,
                      lambda r,b=backend,n=scene:r['engine']==b and r['scene']==n and r['phase_ms']==0 and not r['animation'] and not r['compare'])
        r=frame(scene+' compare','V',lambda r,n=scene:r['scene']==n and r['compare'])
        assert r['compared_pixels']==r['width']**2 and r['mismatch']==-1
    frame('GPU only','G',lambda r:r['engine']=='fpga' and not r['compare'])
    frame('GPU live',' ',lambda r:r['engine']=='fpga' and r['animation'],count=15)
    s.write(b'Q');drain(1);command('')
    record['memory_after']=command('mem --json');record['display_after']=command('displaystatus')
    def memory(text):
        return next(json.loads(line[line.index('{'):])for line in text.splitlines()if '{\"schema\"' in line)
    before,after=memory(record['memory_before']),memory(record['memory_after'])
    for heap in ('internal','psram'):
        assert before[heap]['used']==after[heap]['used'],(heap,before,after)
    assert 'errors=0' in record['display_after'] and 'timeouts=0' in record['display_after']
    record['comparison']={}
    for scene in ('mandelbrot','julia'):
        record['comparison'][scene]={}
        for backend in ('cpu','fpga'):
            rows=[c['reports'][0]for c in record['checks']if c['label'].startswith(scene+' '+backend+' ')]
            record['comparison'][scene][backend]={
                k:statistics.median(r[k]for r in rows)for k in ('frame_ms','cpu_ms','gpu_ms','draw_ms')}
    record['result']='passed'
except Exception as e:
    record['result']='failed';record['error']=str(e);raise
finally:save();s.close()
