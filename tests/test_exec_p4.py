"""Actual P4 execution guard/cache ordering and loader failure cleanup models."""
import argparse,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as t:
    tmp=Path(t)
    headers={'soc/soc.h':'#define SOC_DRAM_LOW 0x4ff00000u\n#define SOC_DRAM_HIGH 0x4ffc0000u\n',
      'sdkconfig.h':'',
      'esp_cache.h':'#define ESP_OK 0\n#define ESP_CACHE_MSYNC_FLAG_DIR_C2M 4\n#define ESP_CACHE_MSYNC_FLAG_DIR_M2C 8\n#define ESP_CACHE_MSYNC_FLAG_TYPE_DATA 16\n#define ESP_CACHE_MSYNC_FLAG_TYPE_INST 32\nint esp_cache_msync(void*,size_t,int);\n',
      'hal/cache_hal.h':'#define CACHE_TYPE_DATA 1\n#define CACHE_TYPE_INSTRUCTION 2\nunsigned cache_hal_get_cache_line_size(unsigned,unsigned);\n',
      'hal/cache_ll.h':'#define CACHE_LL_LEVEL_INT_MEM 1\n'}
    for name,content in headers.items():
        f=tmp/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(content)
    code=(r/'hal/esp32p4/exec_alias.c').read_text(encoding='utf-8')
    code=code.replace('__asm__ volatile ("fence.i" ::: "memory");','model_fence();')
    model='''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
static unsigned calls,fences,fail_at,line=64;
static void model_fence(void){assert(calls==2);fences++;}
int esp_cache_msync(void*p,size_t n,int flags){assert(p==(void*)0x4ff58080u&&n==384);calls++;assert(flags==(calls==1?20:40));return fail_at==calls?-1:0;}
unsigned cache_hal_get_cache_line_size(unsigned level,unsigned type){assert(level==1&&(type==1||type==2));return line;}
'''+code+'''
int main(void){
 void*p=(void*)0x4ff58080u;
#if CONFIG_ESP_SYSTEM_PMP_IDRAM_SPLIT
 assert(!hal_exec_alias(p,260)&&!hal_exec_sync(p,260)&&!calls&&!fences);
#else
 assert(hal_exec_alias(p,260)==(uintptr_t)p);assert(!hal_exec_alias(NULL,4));assert(!hal_exec_alias(p,0));assert(!hal_exec_alias((void*)0x48000000u,4));assert(!hal_exec_alias((void*)0x4ffbfff0u,32));
 assert(hal_exec_sync(p,260)&&calls==2&&fences==1);
 calls=fences=0;fail_at=1;assert(!hal_exec_sync(p,260)&&calls==1&&!fences);
 calls=fences=0;fail_at=2;assert(!hal_exec_sync(p,260)&&calls==2&&!fences);
 calls=fences=fail_at=0;assert(!hal_exec_sync((void*)0x4ff58090u,260)&&!calls);line=256;assert(!hal_exec_sync(p,260)&&!calls);
#endif
 puts("P4 executable RAM guard/cache model passed");return 0;}
'''
    src=tmp/'exec.c';src.write_text(model)
    for split in (0,1):
        exe=tmp/f'exec{split}.exe'
        subprocess.run([a.cc,'cc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror','-I'+str(tmp),'-I'+str(r/'kernel/include'),f'-DCONFIG_ESP_SYSTEM_PMP_IDRAM_SPLIT={split}',str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    loader=(r/'kernel/app/m5ex_loader.c').read_text(encoding='utf-8').split('\nint app_start_file(')[0]
    model=(r/'tests/exec_loader_model.c').read_text(encoding='utf-8').replace('/* ACTUAL_LOADER */',loader)
    src=tmp/'loader.c';src.write_text(model);exe=tmp/'loader.exe'
    subprocess.run([a.cc,'cc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror','-DM5OS_BOARD_TAB5=1','-I'+str(r/'kernel/include'),'-I'+str(r/'kernel/app'),'-I'+str(r),str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
