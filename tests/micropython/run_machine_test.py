"""Actual pinned MicroPython VM on a 32-bit host; IO HAL is a deterministic mock."""
from pathlib import Path
import argparse, subprocess, tempfile, sys, contextlib
r=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);p.add_argument('--settings',type=Path,default=r/'build-tab5-size/esp-idf/main/micropython_cpp.settings');p.add_argument('--work-dir',type=Path);p.add_argument('--reuse-headers',action='store_true');p.add_argument('tests',nargs='*');a=p.parse_args()
# Generate isolated QSTR/module/root tables from the target configuration.
# All native widths match: x86 Windows and ESP32-P4 both use 32-bit pointers/long.
sources=[line[7:] for line in a.settings.read_text().splitlines() if line.startswith('source=') and '/third_party/micropython/py/' in line.replace('\\','/')]
ports=[r/'user/micropython'/n for n in ['modmachine.c','machine_pwm.c','machine_uart.c','machine_buses.c','modtime.c']]
ports=[p for p in ports if p.exists()]
with (contextlib.nullcontext(a.work_dir) if a.work_dir else tempfile.TemporaryDirectory(prefix='meos-mp-machine-')) as d:
 d=Path(d);d.mkdir(parents=True,exist_ok=True);exe=d/'machine.exe'
 settings=d/'cpp.settings';content=a.settings.read_text();content+='\n'+''.join('source='+str(p).replace('\\','/')+'\n' for p in ports if 'source='+str(p).replace('\\','/') not in content);settings.write_text(content)
 if not a.reuse_headers:subprocess.run([sys.executable,str(r/'tools/gen_micropython.py'),'--settings',str(settings),'--vendor',str(r/'third_party/micropython'),'--port',str(r/'user/micropython'),'--output',str(d/'genhdr')],check=True)
 command=[a.cc,'cc','-target','x86-windows-gnu','-std=c11','-O1','-UNDEBUG','-w','-DM5OS_MICROPYTHON=1','-DMICROPY_ENABLE_SOURCE_LINE=1','-DMICROPY_ERROR_REPORTING=3','-D_CRT_SECURE_NO_WARNINGS','-I',str(r/'tests/micropython/machine_stub'),'-I',str(r/'user/micropython'),'-I',str(r/'third_party/micropython'),'-I',str(d),'-I',str(r/'kernel/include')]+sources+[str(p) for p in ports]+[str(r/'tests/micropython/host_machine.c'),'-o',str(exe),'-lm']
 # Avoid Windows command-line length limits via a compiler response file.
 response=d/'args.rsp';response.write_text(' '.join('"'+x.replace('\\','/')+'"' for x in command[2:]))
 subprocess.run(command[:2]+['@'+str(response)],check=True)
 tests=a.tests or ['tests/micropython/machine_functional.py','tests/micropython/uart_safety.py','tests/micropython/pwm_safety.py','tests/micropython/bus_safety.py','tests/micropython/time_safety.py']
 for test in tests:subprocess.run([str(exe),str(r/test)],check=True)
 if not a.tests:subprocess.run([str(exe),str(r/'tests/micropython/exception_cleanup.py'),'--expect-runtime-error'],check=True)
