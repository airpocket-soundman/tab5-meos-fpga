import argparse,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args();r=Path(__file__).resolve().parents[1]
variants=[{'ESP_IDF_VERSION_MAJOR':4},{'ESP_IDF_VERSION_MAJOR':4,'CONFIG_SPIRAM_BOOT_INIT':0},{}, {'CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY':1}, {'CONFIG_SPIRAM_ALLOW_NOINIT_SEG_EXTERNAL_MEMORY':1}, {'CONFIG_SPIRAM_BOOT_INIT':0}, {'CONFIG_SPIRAM_USE_MEMMAP':0}, {'CONFIG_SPIRAM_FETCH_INSTRUCTIONS':1}, {'CONFIG_SPIRAM_RODATA':1}]
for variant in variants:
 defs={'CONFIG_SPIRAM_BOOT_INIT':1,'CONFIG_SPIRAM_USE_MEMMAP':1,'CONFIG_MMU_PAGE_SIZE':65536,**variant}
 with tempfile.TemporaryDirectory() as t:
  exe=Path(t)/'cores3psram.exe'
  subprocess.run([a.cc,'cc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror','-DM5OS_BOARD_CORES3=1',*['-D%s=%s'%x for x in defs.items()],'-I'+str(r/'kernel/include'),'-I'+str(r/'tools/psram/stubs'),str(r/'tools/psram/cores3_test.c'),str(r/'hal/esp32s3/mem_psram.c'),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
