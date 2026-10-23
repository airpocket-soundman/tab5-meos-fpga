"""Compile actual FAT adapter/FatFs and test exclusive create on a RAM FAT12 image."""
from pathlib import Path
import argparse,struct,subprocess,tempfile
r=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='meos-fat-excl-') as d:
    d=Path(d);image=d/'disk.img';exe=d/'test.exe'
    b=bytearray(1474560);b[:3]=bytes.fromhex('eb3c90');b[3:11]=b'MEOSQA  '
    struct.pack_into('<HBHBHHBHHHII',b,11,512,1,1,2,224,2880,0xf0,9,18,2,0,0)
    b[38]=0x29;b[43:54]=b'MEOS TEST  ';b[54:62]=b'FAT12   ';b[510:512]=bytes.fromhex('55aa')
    for offset in (512,5120):b[offset:offset+6]=bytes.fromhex('f0ffffff0f00')
    offset=19*512;b[offset:offset+11]=b'HELLO   TXT';b[offset+11]=0x20
    struct.pack_into('<H',b,offset+26,2);struct.pack_into('<I',b,offset+28,6);b[33*512:33*512+6]=b'hello\n'
    image.write_bytes(b)
    subprocess.run([a.cc,'cc','-O1','-UNDEBUG','-Wall','-Wextra','-I',str(r/'kernel/include'),'-I',str(r/'third_party/fatfs'),'-I',str(r/'build-tab5-size/config'),str(r/'tests/storage/fat_excl_test.c'),str(r/'third_party/fatfs/ff.c'),str(r/'third_party/fatfs/ffunicode.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(image)],check=True)
    unsupported=d/'unsupported.exe'
    subprocess.run([a.cc,'cc','-O1','-UNDEBUG','-Wall','-Wextra','-I',str(r/'kernel/include'),str(r/'tests/storage/excl_unsupported_test.c'),str(r/'fs/m5fs/m5fs.c'),str(r/'fs/vfs/devfs.c'),'-o',str(unsupported)],check=True)
    subprocess.run([str(unsupported)],check=True)
