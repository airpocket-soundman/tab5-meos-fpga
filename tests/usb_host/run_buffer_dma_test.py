from pathlib import Path
import argparse, subprocess, tempfile
root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--cc', required=True)
a = p.parse_args()
with tempfile.TemporaryDirectory(prefix='meos-usb-dma-') as d:
    exe = Path(d) / 'buffer-dma-test.exe'
    subprocess.run([a.cc, 'cc', '-std=gnu11', '-O1', '-UNDEBUG', '-I', str(root/'kernel/include'), str(root/'tests/usb_host/buffer_dma_test.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
