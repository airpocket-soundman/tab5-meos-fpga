/* Actual daemon decisions with mocked resources; never touches hardware. */
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <m5os/suspend.h>
#include <m5os/io.h>
#include <m5os/task.h>
static bool display_busy,input_busy,io_busy;
static unsigned hardware,clock_mhz=360,irq_depth,clock_changes;
static uint64_t clock_us;
uint32_t arch_irq_disable(void){return irq_depth++;}
void arch_irq_restore(uint32_t previous){assert(irq_depth>previous);irq_depth=previous;}
uint64_t hal_time_now_us(void){return clock_us;}
bool display_suspend_busy(void){return display_busy;}
bool kconsole_suspend_busy(void){return input_busy;}
int io_pin_info(int p,io_pin_info_t*out){memset(out,0,sizeof*out);out->owner=io_busy&&p==18;return 0;}
void board_backlight(uint8_t n){(void)n;hardware++;}
uint8_t board_backlight_get(void){return 123;}
bool board_ext5v_set(bool on,bool*was){(void)on;if(was)*was=true;hardware++;return true;}
void display_panel_power(bool on){(void)on;hardware++;}
void display_set_suspended(bool on){(void)on;hardware++;}
unsigned hal_cpu_mhz(void){return clock_mhz;}
bool hal_cpu_set_mhz(unsigned mhz){clock_mhz=mhz;clock_changes++;hardware++;return true;}
void kconsole_request_refresh(void){hardware++;}
void kconsole_set_input_hook(void(*hook)(void)){assert(hook);}
void kprintf(const char*format,...){(void)format;}
void kputs(const char*s){(void)s;}
void task_sleep(uint32_t ms){(void)ms;}
int task_create_stack(const char*n,void(*entry)(void*),void*arg,uint8_t priority,size_t size){(void)n;(void)arg;(void)priority;assert(entry&&size==6144);return 1;}
#include "../kernel/boot/suspend.c"
int main(void){
 suspend_start();
#if M5OS_MODULE_FPGA || M5OS_MODULE_FPGA_PARLIO
 assert(s_core.idle_seconds==0);
#else
 assert(s_core.idle_seconds==SUSPEND_DEFAULT_IDLE_SECONDS);
#endif
 uint32_t fixed=suspend_inhibit_reasons();assert(fixed==EXPECTED_FIXED);
 assert(!suspend_inhibit_acquire(0)&&!suspend_inhibit_acquire(3));
 assert(!suspend_inhibit_release(SUSPEND_INHIBIT_OTHER));
 assert(suspend_inhibit_acquire(SUSPEND_INHIBIT_OTHER));
 assert(suspend_inhibit_acquire(SUSPEND_INHIBIT_OTHER));
 assert(suspend_inhibit_reasons()&SUSPEND_INHIBIT_OTHER);
 assert(!suspend_try_request());do_enter();assert(!hardware&&!s_core.suspended);
 assert(suspend_inhibit_release(SUSPEND_INHIBIT_OTHER));assert(suspend_inhibit_reasons()&SUSPEND_INHIBIT_OTHER);
 assert(suspend_inhibit_release(SUSPEND_INHIBIT_OTHER));assert(suspend_inhibit_reasons()==fixed);
 display_busy=true;assert(suspend_inhibit_reasons()&SUSPEND_INHIBIT_DISPLAY);assert(!suspend_try_request());do_enter();assert(!hardware);display_busy=false;
 input_busy=true;assert(suspend_inhibit_reasons()&SUSPEND_INHIBIT_INPUT);assert(!suspend_try_request());do_enter();assert(!hardware);input_busy=false;
 io_busy=true;assert(suspend_inhibit_reasons()&SUSPEND_INHIBIT_IO);assert(!suspend_try_request());do_enter();assert(!hardware);io_busy=false;
 if(fixed){
  assert(!suspend_try_request());s_core.idle_seconds=10;clock_us=11000000;assert(suspend_core_step(&s_core,clock_us)==SUSPEND_ENTER);
  do_enter();assert(!hardware&&!s_core.suspended&&!s_core.enter_requested);
  assert(s_core.last_activity_us==clock_us); /* Recheck prevents repeated auto-entry attempts. */
 }else{
  assert(suspend_try_request());display_busy=true;do_enter();
  assert(s_core.enter_requested&&!s_core.suspended&&!hardware);
  clock_us+=200000;display_busy=false;do_enter();assert(s_core.suspended&&clock_mhz==360&&hardware&&!clock_changes);
  assert(!suspend_inhibit_acquire(SUSPEND_INHIBIT_OTHER));
  assert(suspend_filter_input());do_wake();assert(!s_core.suspended&&!s_entering&&clock_mhz==360);
  assert(suspend_inhibit_acquire(SUSPEND_INHIBIT_OTHER));assert(suspend_inhibit_release(SUSPEND_INHIBIT_OTHER));
  unsigned prior=hardware;
  assert(suspend_try_request());display_busy=true;clock_us+=REQUEST_RENDER_WAIT_US;do_enter();
  assert(!s_core.enter_requested&&!s_core.suspended&&hardware==prior);display_busy=false;
  assert(suspend_try_request());io_busy=true;do_enter();
  assert(!s_core.enter_requested&&!s_core.suspended&&hardware==prior);io_busy=false;
 }
 assert(!irq_depth&&!clock_changes);puts("Suspend safety: static unsupported-resource masks, IO/display/input leases, refcounts, manual/auto BUSY and wake guard passed");return 0;
}
