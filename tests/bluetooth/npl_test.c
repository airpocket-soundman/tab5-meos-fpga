#include <assert.h>
#include <stdio.h>
#include "../../drivers/bluetooth/npl/meos_npl.inc"
static uint64_t now_us;
static task_t tasks[2];static unsigned current;
static unsigned irq_level;
uint32_t arch_irq_disable(void) {uint32_t old=irq_level;irq_level=1;return old;}
void arch_irq_restore(uint32_t flags) {irq_level=flags;}
bool arch_irq_enabled(void) {return !irq_level;}
uint64_t hal_time_now_us(void) {return now_us;}
void task_sleep(uint32_t ms) {assert(!irq_level);now_us+=(uint64_t)ms*1000;}
void task_yield(void) {assert(!irq_level);now_us+=1000;}
task_t *task_current(void) {return &tasks[current];}
bool sched_started(void) {return true;}
static unsigned callbacks;
static void callback(struct ble_npl_event *ev) {assert(!irq_level);assert(ble_npl_event_get_arg(ev)==&callbacks);callbacks++;}
int main(void) {
 struct ble_npl_eventq q,other;ble_npl_eventq_init(&q);ble_npl_eventq_init(&other);
 struct ble_npl_event a,b;ble_npl_event_init(&a,callback,&callbacks);ble_npl_event_init(&b,callback,&callbacks);
 ble_npl_eventq_put(&q,&a);ble_npl_eventq_put(&q,&a);ble_npl_eventq_put(&other,&a);
 ble_npl_eventq_put(&q,&b);assert(ble_npl_eventq_get(&q,0)==&a);
 assert(ble_npl_eventq_get(&q,0)==&b);assert(!ble_npl_eventq_get(&q,2));assert(now_us==2000);
 ble_npl_eventq_put(&q,&a);ble_npl_eventq_put(&q,&b);ble_npl_eventq_remove(&q,&a);
 assert(ble_npl_eventq_get(&q,0)==&b);assert(ble_npl_eventq_is_empty(&q));
 static struct ble_npl_callout co;ble_npl_callout_init(&co,&q,callback,&callbacks);
 assert(ble_npl_callout_reset(&co,5)==BLE_NPL_OK);assert(ble_npl_callout_remaining_ticks(&co,2)==5);
 struct ble_npl_event *ev=ble_npl_eventq_get(&q,10);assert(ev==&co.event);ble_npl_event_run(ev);assert(callbacks==1);
 assert(!ble_npl_callout_is_active(&co));assert(ble_npl_callout_reset(&co,1)==BLE_NPL_OK);
 now_us+=1000;npl_service_callouts();assert(ble_npl_event_is_queued(&co.event));ble_npl_callout_stop(&co);
 assert(ble_npl_eventq_is_empty(&q));
 /* Reinitialization must detach a queued timer, preserving queue ownership. */
 assert(ble_npl_callout_reset(&co,1)==BLE_NPL_OK);now_us+=1000;npl_service_callouts();
 assert(ble_npl_event_is_queued(&co.event));ble_npl_callout_init(&co,&q,callback,&callbacks);
 assert(ble_npl_eventq_is_empty(&q)&&!co.active);
 /* 32-bit tick rollover uses signed deadline comparisons. */
 now_us=((uint64_t)UINT32_MAX-1)*1000;assert(ble_npl_callout_reset(&co,4)==BLE_NPL_OK);
 ev=ble_npl_eventq_get(&q,5);assert(ev==&co.event&&ble_npl_time_get()==2);
 struct ble_npl_mutex mu;assert(ble_npl_mutex_init(&mu)==BLE_NPL_OK);
 assert(ble_npl_mutex_pend(&mu,0)==BLE_NPL_OK);assert(ble_npl_mutex_pend(&mu,0)==BLE_NPL_OK);
 current=1;assert(ble_npl_mutex_pend(&mu,2)==BLE_NPL_TIMEOUT);assert(ble_npl_mutex_release(&mu)==BLE_NPL_BAD_MUTEX);
 current=0;assert(ble_npl_mutex_deinit(&mu)==BLE_NPL_EBUSY);
 assert(ble_npl_mutex_release(&mu)==BLE_NPL_OK);assert(mu.owner);
 assert(ble_npl_mutex_release(&mu)==BLE_NPL_OK);assert(!mu.owner);assert(ble_npl_mutex_deinit(&mu)==BLE_NPL_OK);
 struct ble_npl_sem sem;assert(ble_npl_sem_init(&sem,1)==BLE_NPL_OK);
 assert(ble_npl_sem_pend(&sem,0)==BLE_NPL_OK);assert(ble_npl_sem_pend(&sem,2)==BLE_NPL_TIMEOUT);
 assert(ble_npl_sem_release(&sem)==BLE_NPL_OK);assert(ble_npl_sem_get_count(&sem)==1);
 uint32_t f=ble_npl_hw_enter_critical();assert(ble_npl_sem_pend(&sem,0)==BLE_NPL_ERR_IN_ISR);ble_npl_hw_exit_critical(f);
 assert(ble_npl_sem_pend(&sem,0)==BLE_NPL_OK);
 ble_npl_eventq_put(&q,&a);ble_npl_eventq_deinit(&q);assert(!ble_npl_event_is_queued(&a));
 assert(!ble_npl_hw_is_in_critical()&&!irq_level);
 static struct ble_npl_callout many[MEOS_NPL_CALLOUTS];
 for(unsigned i=0;i<MEOS_NPL_CALLOUTS;i++)ble_npl_callout_init(&many[i],&other,callback,&callbacks);
 assert(!many[MEOS_NPL_CALLOUTS-1].registered);
 assert(ble_npl_callout_reset(&many[MEOS_NPL_CALLOUTS-1],1)==BLE_NPL_ENOMEM);
 puts("MeOS NimBLE NPL queue/timer/synchronization tests PASS");
}
