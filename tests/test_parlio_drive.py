"""Actual native drive save/apply/restore helpers, without physical GPIO."""
import argparse
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
source=(root/'hal/esp32p4/parlio_native.c').read_text()
helpers=source[source.index('static int drive_pin'):source.index('static mutex_t resource_lock')]
close=source[source.index('bool hal_parlio_close'):]
# Restoration must remain unreachable on either unproven stream closure or
# finite DMA quarantine; successful close first leaves data/clock as inputs.
assert close.index('if(!hal_parlio_stream_stop(c))return false;')<close.index('restore_drive(c);')
quarantine=close.index('if(tx_stop||rx_stop||c->dma_quarantined)')
assert close.index('return false;',quarantine)<close.index('restore_drive(c);')
assert 'restore_bus(false);\n    restore_drive(c);' in close
model=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
typedef unsigned gpio_drive_cap_t;
enum{GPIO_DRIVE_CAP_0=0,CLOCK_PIN=16,DIR_PIN=51};
static const int data_pins[8]={18,19,5,3,4,2,48,47};
typedef struct{bool drive_saved;gpio_drive_cap_t drive_previous[10];}hal_parlio_t;
static int GPIO;static unsigned caps[55],original[55],writes;static bool write_fault;
static void gpio_ll_get_drive_capability(void*h,unsigned p,gpio_drive_cap_t*out){assert(h==&GPIO&&p<55);*out=caps[p];}
static void gpio_ll_set_drive_capability(void*h,unsigned p,gpio_drive_cap_t value){assert(h==&GPIO&&p<55&&value<4);writes++;if(!(write_fault&&p==18))caps[p]=value;}
/* HELPERS */
static bool owned(unsigned p){for(unsigned i=0;i<10;i++)if((unsigned)drive_pin(i)==p)return true;return false;}
int main(void){
 for(unsigned p=0;p<55;p++)original[p]=caps[p]=1+p%3;
 hal_parlio_t c={0};assert(apply_drive(&c)&&c.drive_saved&&writes==10);
 for(unsigned p=0;p<55;p++)assert(caps[p]==(owned(p)?0:original[p]));
 assert(apply_drive(&c)); /* Re-apply never saves our own CAP0 as the old state. */
 for(unsigned i=0;i<10;i++)assert(c.drive_previous[i]==original[drive_pin(i)]);
 /* Quarantine must retain this exact state; no restore is called until quiet. */
 for(unsigned p=0;p<55;p++)assert(caps[p]==(owned(p)?0:original[p]));
 restore_drive(&c);assert(!c.drive_saved);
 for(unsigned p=0;p<55;p++)assert(caps[p]==original[p]);
 unsigned before=writes;restore_drive(&c);assert(writes==before);
 write_fault=true;assert(!apply_drive(&c)&&c.drive_saved);write_fault=false;
 restore_drive(&c);for(unsigned p=0;p<55;p++)assert(caps[p]==original[p]);
 puts("Drive parity: exact DATA8/CLK/DIR CAP0, prior capture, repeated apply, readback failure and safe restoration passed");return 0;
}
'''.replace('/* HELPERS */',helpers)
with tempfile.TemporaryDirectory(prefix='meos-drive-') as tmp:
    src=Path(tmp)/'drive.c';src.write_text(model);exe=Path(tmp)/'drive.exe'
    subprocess.run([a.cc,'cc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
