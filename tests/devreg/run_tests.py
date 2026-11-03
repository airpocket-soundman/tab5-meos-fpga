"""Host test of kernel/dev/devreg.c.  python tests/devreg/run_tests.py [--cc ZIG]  (default: Visual Studio's cl)"""
from pathlib import Path
import argparse
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser()
ap.add_argument('--cc', help='zig executable (runs "zig cc"); omit to use Visual Studio through vswhere')
args = ap.parse_args()
srcs = [str(root / 'tests/devreg/test_devreg.c'), str(root / 'kernel/dev/devreg.c')]
inc = str(root / 'kernel/include')
with tempfile.TemporaryDirectory(prefix='meos-devreg-') as d:
    exe = Path(d) / 'devreg-test.exe'
    if args.cc:
        subprocess.run([args.cc, 'cc', '-O1', '-UNDEBUG', '-Wall', '-I', inc, *srcs, '-o', str(exe)], check=True)
    else:
        vswhere = Path(r'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe')
        vs = subprocess.run([str(vswhere), '-latest', '-products', '*', '-property', 'installationPath'],
                            capture_output=True, text=True, check=True).stdout.strip()
        bat = Path(d) / 'build.bat'
        bat.write_text(f'@call "{vs}\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\n'
                       f'@cl /nologo /Od /W3 /I "{inc}" ' + ' '.join(f'"{s}"' for s in srcs) +
                       f' /Fe:"{exe}" /Fo:"{d}\\\\"\r\n')
        r = subprocess.run(['cmd', '/c', str(bat)], capture_output=True, text=True)
        if r.returncode:
            print(r.stdout, r.stderr)
            raise SystemExit('build failed')
    subprocess.run([str(exe)], check=True)
