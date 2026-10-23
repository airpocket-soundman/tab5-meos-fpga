"""Run LCD history regressions on the host (no device access)."""
from pathlib import Path
import argparse
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--cc', required=True, help='Path to Zig executable')
args = p.parse_args()
with tempfile.TemporaryDirectory(prefix='meos-console-') as work:
    exe = Path(work) / 'history-test.exe'
    subprocess.run([args.cc, 'cc', '-O1', '-UNDEBUG', '-DM5OS_BOARD_TAB5=1',
                    '-I', str(root / 'kernel/include'),
                    str(root / 'tests/console/history_test.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
