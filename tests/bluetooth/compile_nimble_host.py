"""Cross-compile the pinned Apache NimBLE host + real SMP crypto into a scratch
archive. This is an integration check, NOT a working BLE app or firmware build.
The upstream checkout is external; nothing is downloaded or vendored implicitly.
"""
from pathlib import Path
import argparse,subprocess,json
r=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--nimble',required=True);p.add_argument('--cc',required=True);p.add_argument('--out',required=True)
a=p.parse_args();n=Path(a.nimble);out=Path(a.out);out.mkdir(parents=True,exist_ok=True)
assert (n/'LICENSE').is_file() and (n/'ext/tinycrypt/src/ecc.c').is_file(), 'Apache NimBLE 1.8.0 source tree required'
groups=['porting/nimble/src','nimble/src','nimble/host/src','nimble/host/util/src','nimble/host/services/gap/src','nimble/host/services/gatt/src','nimble/host/store/ram/src','nimble/transport/src','ext/tinycrypt/src']
exclude={'hal_timer.c','os_cputime.c','os_cputime_pwr2.c','ble_gatts_lcl.c'}
sources=[s for g in groups for s in sorted((n/g).glob('*.c')) if s.name not in exclude]
incs=[r/'drivers/bluetooth/npl',n/'nimble/include',n/'nimble/host/include',n/'nimble/host/util/include',n/'nimble/host/services/gap/include',n/'nimble/host/services/gatt/include',n/'nimble/host/store/ram/include',n/'nimble/transport/include',n/'porting/nimble/include',n/'ext/tinycrypt/include',r/'kernel/include']
defines=['NIMBLE_CFG_CONTROLLER=0','MYNEWT_VAL_BLE_ROLE_PERIPHERAL=0','MYNEWT_VAL_BLE_ROLE_BROADCASTER=0','MYNEWT_VAL_BLE_SM_SC=1','MYNEWT_VAL_BLE_SM_SC_ONLY=1','MYNEWT_VAL_BLE_SM_LEGACY=0','MYNEWT_VAL_BLE_MAX_CONNECTIONS=1','MYNEWT_VAL_BLE_TRANSPORT_HS__native=1','MYNEWT_VAL_BLE_TRANSPORT_LL__native=0','MYNEWT_VAL_BLE_TRANSPORT_LL__custom=1','MYNEWT_VAL_BLE_TRANSPORT_LL__socket=0','MYNEWT_VAL_BLE_TRANSPORT_EVT_SIZE=257','MYNEWT_VAL_BLE_TRANSPORT_ACL_FROM_HS_COUNT=4','MYNEWT_VAL_BLE_TRANSPORT_ACL_FROM_LL_COUNT=4','MYNEWT_VAL_BLE_TRANSPORT_EVT_DISCARDABLE_COUNT=4']
base=[a.cc,'-std=gnu11','-Os','-ffunction-sections','-fdata-sections','-march=rv32imafc','-mabi=ilp32f']
base += ['-D'+d for d in defines]
for inc in incs:base+=['-I',str(inc)]
objects=[]
for index,src in enumerate(sources):
 obj=out/f'{index:03d}-{src.stem}.o'
 subprocess.run(base+(['-Dsprintf=meos_bt_sprintf'] if src.name=='ble_uuid.c' else [])+['-Dmalloc=meos_bt_malloc','-Dcalloc=meos_bt_calloc','-Drealloc=meos_bt_realloc','-Dfree=meos_bt_free','-c',str(src),'-o',str(obj)],check=True)
 objects.append(obj)
# The MeOS NPL stays .inc until a full host application selects it.
unit=out/'meos_npl.c';unit.write_text('#include "'+(r/'drivers/bluetooth/npl/meos_npl.inc').as_posix()+'"\n')
obj=out/'meos_npl.o';subprocess.run(base+['-Wall','-Wextra','-Werror','-c',str(unit),'-o',str(obj)],check=True);objects.append(obj)
bridge=out/'hosted_nimble.c';bridge.write_text('#include <m5os/arch.h>\n#include <m5os/task.h>\n'+''.join('#include "'+(r/('drivers/bluetooth/npl/'+part)).as_posix()+'"\n' for part in ['hosted_nimble.inc','nimble_app.inc','meos_allocator.inc','meos_text.inc']))
obj=out/'hosted_nimble.o';subprocess.run(base+['-Wall','-Wextra','-Werror','-c',str(bridge),'-o',str(obj)],check=True);objects.append(obj)
ar=str(Path(a.cc).with_name('riscv32-esp-elf-ar.exe'))
archive=out/'libnimble-meos-preparation.a';subprocess.run([ar,'rcs',str(archive)]+[str(o) for o in objects],check=True)
ld=str(Path(a.cc).with_name('riscv32-esp-elf-ld.exe'));nm=str(Path(a.cc).with_name('riscv32-esp-elf-nm.exe'))
combined=out/'combined.o';subprocess.run([ld,'-r','--whole-archive',str(archive),'--no-whole-archive','-o',str(combined)],check=True)
undefined=subprocess.check_output([nm,'-u',str(combined)],text=True)
assert not any(line.split()[-1].startswith(('ble_','nimble_','os_','meos_bt_')) for line in undefined.splitlines()),undefined
assert not any(line.split()[-1] in ('malloc','calloc','realloc','free') for line in undefined.splitlines()),undefined
(out/'undefined.txt').write_text(undefined)
(out/'manifest.json').write_text(json.dumps({'upstream':'Apache NimBLE nimble_1_8_0_tag','defines':defines,'includes':[str(i) for i in incs],'sources':[str(s) for s in sources],'archive':str(archive),'status':'cross-compiled application and real host/SMP/GATT with relocatable link audit; hardware pairing not tested'},indent=2))
print(f'Cross-compiled {len(objects)} objects including real SMP SC/TinyCrypt and MeOS NPL: {archive}')
