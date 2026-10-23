from pathlib import Path
import argparse, subprocess, tempfile
root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--cc', required=True)
a = p.parse_args()
with tempfile.TemporaryDirectory(prefix='meos-soft-io-') as tmp:
    for name in ('i2c', 'spi'):
        exe = Path(tmp) / (name + '.exe')
        subprocess.run([a.cc, 'cc', '-O1', '-UNDEBUG', '-Wall', '-Wextra',
                        '-I', str(root / 'kernel/include'),
                        str(root / 'tests/io' / (name + '_test.c')), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
