"""Run the imported FatFs against a disposable FAT12 image (no device access).
Usage: python tests/usb_host/run_fat_test.py --cc C:/path/to/zig.exe
Requires an existing build-tab5-size/config/sdkconfig.h and Zig's C compiler.
"""
from pathlib import Path
import argparse, subprocess, struct, tempfile
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);args=p.parse_args()
with tempfile.TemporaryDirectory(prefix='meos-fat-') as work:
    work=Path(work);image=work/'disk.img';exe=work/'fat-test.exe'
    b=bytearray(1474560);b[:3]=bytes.fromhex('eb3c90');b[3:11]=b'MEOSQA  '
    struct.pack_into('<HBHBHHBHHHII',b,11,512,1,1,2,224,2880,0xf0,9,18,2,0,0)
    b[38]=0x29;b[43:54]=b'MEOS TEST  ';b[54:62]=b'FAT12   ';b[510:512]=bytes.fromhex('55aa')
    for off in [512,5120]:b[off:off+6]=bytes.fromhex('f0ffffff0f00')
    off=19*512;b[off:off+11]=b'HELLO   TXT';b[off+11]=0x20
    struct.pack_into('<H',b,off+26,2);struct.pack_into('<I',b,off+28,6);b[33*512:33*512+6]=b'hello\n'
    image.write_bytes(b)
    subprocess.run([args.cc,'cc','-O2','-I',str(root/'third_party/fatfs'),'-I',str(root/'build-tab5-size/config'),str(root/'tests/usb_host/fat_image_test.c'),str(root/'third_party/fatfs/ff.c'),str(root/'third_party/fatfs/ffunicode.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(image)],check=True)
