"""Host tests; Windows I2C concurrency test uses Win32 threads, no device access."""
import argparse,pathlib,subprocess,tempfile
ROOT=pathlib.Path(__file__).resolve().parents[2]
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--cc',required=True);args=parser.parse_args()
 with tempfile.TemporaryDirectory(prefix='meos-camera-') as temp:
  out=pathlib.Path(temp)
  source=(ROOT/'drivers/camera/camera_tab5.c').read_text(encoding='utf-8')
  lifecycle=source[source.index('static void free_frames'):source.index('bool camera_set_mirror')]
  glue=(ROOT/'tests/camera/test_lifecycle_glue.c').read_text(encoding='utf-8').replace('/* CAMERA_LIFECYCLE */',lifecycle)
  generated=out/'lifecycle.c';generated.write_text(glue,encoding='utf-8')
  for name,c in [('bmp',ROOT/'tests/camera/test_bmp.c'),('i2c',ROOT/'tests/camera/test_i2c_lock.c'),('lifecycle',generated)]:
   exe=out/(name+'.exe')
   subprocess.run([args.cc,'cc','-O1','-DM5OS_BOARD_TAB5=1','-I',str(ROOT/'kernel/include'),'-I',str(ROOT/'drivers/camera'),str(c),'-o',str(exe)],check=True)
   subprocess.run([str(exe)],check=True)
if __name__=='__main__':main()
