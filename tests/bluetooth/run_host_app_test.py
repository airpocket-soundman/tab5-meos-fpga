from pathlib import Path
import argparse,subprocess,tempfile
r=Path(__file__).resolve().parents[2];n=r/'third_party/nimble'
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
incs=[r/'kernel/include',r/'drivers/bluetooth/npl',n/'nimble/include',n/'nimble/transport/include',n/'nimble/host/include',n/'nimble/host/util/include',n/'nimble/host/store/ram/include',n/'porting/nimble/include']
with tempfile.TemporaryDirectory(prefix='meos-host-app-') as d:
 e=Path(d)/'host-app-test.exe'
 cmd=[a.cc,'cc','-std=c11','-Wall','-Wextra','-Werror','-O1','-UNDEBUG','-DNIMBLE_CFG_CONTROLLER=0']
 for inc in incs:cmd+=['-I',str(inc)]
 cmd += [str(r/'tests/bluetooth/host_app_test.c'),str(r/'drivers/bluetooth/hid_boot.c'),str(r/'drivers/bluetooth/hogp_session.c'),'-o',str(e)]
 subprocess.run(cmd,check=True);subprocess.run([str(e)],check=True)
 alloc=Path(d)/'allocator-test.exe'
 subprocess.run([a.cc,'cc','-std=c11','-Wall','-Wextra','-Werror','-O1','-UNDEBUG','-I',str(r/'kernel/include'),str(r/'tests/bluetooth/allocator_test.c'),'-o',str(alloc)],check=True)
 subprocess.run([str(alloc)],check=True)
 text=Path(d)/'text-test.exe'
 subprocess.run([a.cc,'cc','-std=c11','-Wall','-Wextra','-Werror','-O1','-UNDEBUG',str(r/'tests/bluetooth/text_test.c'),'-o',str(text)],check=True)
 subprocess.run([str(text)],check=True)
 bridge=Path(d)/'bridge-test.exe'
 bridge_cmd=[a.cc,'cc','-std=c11','-Wall','-Wextra','-Werror','-O1','-UNDEBUG','-DNIMBLE_CFG_CONTROLLER=0','-DMYNEWT_VAL_BLE_TRANSPORT_EVT_SIZE=257']
 for inc in incs:bridge_cmd+=['-I',str(inc)]
 subprocess.run(bridge_cmd+[str(r/'tests/bluetooth/hosted_bridge_test.c'),'-o',str(bridge)],check=True)
 subprocess.run([str(bridge)],check=True)
