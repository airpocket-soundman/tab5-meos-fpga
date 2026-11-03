from pathlib import Path
import argparse,subprocess,tempfile
r=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser()
p.add_argument('--cc',help='zig executable (runs "zig cc"); omit to use Visual Studio (cl) found through vswhere')
a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='meos-uvc-') as d:
 e=Path(d)/'uvc-test.exe'
 src=str(r/'tests/usb_host/uvc_test.c')
 if a.cc:
  subprocess.run([a.cc,'cc','-O1','-UNDEBUG','-I',str(r/'kernel/include'),src,'-o',str(e)],check=True)
 else:
  vswhere=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe')
  root=subprocess.run([str(vswhere),'-latest','-products','*','-property','installationPath'],capture_output=True,text=True,check=True).stdout.strip()
  bat=Path(d)/'build.bat'
  bat.write_text(f'@call "{root}\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\n@cl /nologo /O1 /W2 /I "{r/"kernel/include"}" "{src}" /Fe:"{e}" /Fo:"{d}\\\\"\r\n')
  res=subprocess.run(['cmd','/c',str(bat)],capture_output=True,text=True)
  if res.returncode:
   print(res.stdout,res.stderr);raise SystemExit('build failed')
 subprocess.run([str(e),str(r/'tests/usb_host/fixtures/elecom_056e_700a.bin')],check=True)
