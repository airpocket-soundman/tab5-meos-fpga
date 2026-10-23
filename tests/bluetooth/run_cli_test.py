from pathlib import Path
import argparse,subprocess,tempfile
r=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='meos-bt-cli-') as d:
 for enabled in (True,False):
  exe=Path(d)/('enabled.exe' if enabled else 'disabled.exe')
  sources=[r/'tests/bluetooth/cli_test.c',r/'drivers/usbkbd/hid_keys.c'] if enabled else [r/'tests/bluetooth/cli_disabled_test.c',r/'user/bluetooth/bt_app.c']
  cmd=[a.cc,'cc','-std=c11','-Wall','-Wextra','-Werror','-Wundef','-O1','-UNDEBUG','-DM5OS_BOARD_TAB5=1','-DM5OS_BLUETOOTH='+str(int(enabled)),'-I',str(r/'kernel/include')]
  subprocess.run(cmd+[str(s) for s in sources]+['-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
