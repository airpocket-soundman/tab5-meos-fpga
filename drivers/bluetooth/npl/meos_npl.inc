/* Original MeOS NimBLE NPL implementation. Include in a dedicated translation
 * unit only when the audited upstream NimBLE host is selected in the build.
 * Single-core MeOS kernel: IRQ masking protects queue and token ownership.
 * Callouts are serviced by eventq_get and blocking waits; no ISR callbacks. */
#include "nimble/nimble_npl.h"
#include <m5os/arch.h>
#include <m5os/task.h>
#include <m5os/hal_time.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#define MEOS_NPL_CALLOUTS 32
static struct ble_npl_callout *npl_callouts[MEOS_NPL_CALLOUTS];
static unsigned npl_critical_depth;
static bool expired(uint32_t now,uint32_t deadline) {return (int32_t)(now-deadline)>=0;}
static bool timed_out(uint32_t start,uint32_t timeout) {
    return timeout!=BLE_NPL_TIME_FOREVER&&(uint32_t)(ble_npl_time_get()-start)>=timeout;
}
uint32_t ble_npl_hw_enter_critical(void) {uint32_t flags=arch_irq_disable();npl_critical_depth++;return flags;}
void ble_npl_hw_exit_critical(uint32_t flags) {if(npl_critical_depth)npl_critical_depth--;arch_irq_restore(flags);}
bool ble_npl_hw_is_in_critical(void) {return npl_critical_depth!=0;}
bool ble_npl_os_started(void) {return sched_started();}
void *ble_npl_get_current_task_id(void) {return task_current();}
ble_npl_time_t ble_npl_time_get(void) {return (uint32_t)(hal_time_now_us()/1000u);}
ble_npl_error_t ble_npl_time_ms_to_ticks(uint32_t ms,ble_npl_time_t *out) {
    if(!out)return BLE_NPL_INVALID_PARAM;
    *out=ms;return BLE_NPL_OK;
}
ble_npl_error_t ble_npl_time_ticks_to_ms(ble_npl_time_t ticks,uint32_t *out) {
    if(!out)return BLE_NPL_INVALID_PARAM;
    *out=ticks;return BLE_NPL_OK;
}
ble_npl_time_t ble_npl_time_ms_to_ticks32(uint32_t ms) {return ms;}
uint32_t ble_npl_time_ticks_to_ms32(ble_npl_time_t ticks) {return ticks;}
void ble_npl_time_delay(ble_npl_time_t ticks) {if(ticks)task_sleep(ticks);else task_yield();}
void ble_npl_eventq_init(struct ble_npl_eventq *q) {memset(q,0,sizeof *q);q->initialized=true;}
void ble_npl_event_init(struct ble_npl_event *ev,ble_npl_event_fn *fn,void *arg) {
    memset(ev,0,sizeof *ev);ev->fn=fn;ev->arg=arg;
}
void *ble_npl_event_get_arg(struct ble_npl_event *ev) {return ev->arg;}
void ble_npl_event_set_arg(struct ble_npl_event *ev,void *arg) {ev->arg=arg;}
bool ble_npl_event_is_queued(struct ble_npl_event *ev) {return ev->queue!=NULL;}
void ble_npl_event_run(struct ble_npl_event *ev) {if(ev&&ev->fn)ev->fn(ev);}
void ble_npl_eventq_put(struct ble_npl_eventq *q,struct ble_npl_event *ev) {
    uint32_t flags=ble_npl_hw_enter_critical();
    if(q&&q->initialized&&ev&&!ev->queue) {
        ev->queue=q;ev->next=NULL;
        if(q->tail)q->tail->next=ev;else q->head=ev;
        q->tail=ev;
    }
    ble_npl_hw_exit_critical(flags);
}
void ble_npl_eventq_remove(struct ble_npl_eventq *q,struct ble_npl_event *ev) {
    uint32_t flags=ble_npl_hw_enter_critical();
    if(q&&ev&&ev->queue==q) {
        struct ble_npl_event *previous=NULL,*at=q->head;
        while(at&&at!=ev) {previous=at;at=at->next;}
        if(at) {
            if(previous)previous->next=at->next;else q->head=at->next;
            if(q->tail==at)q->tail=previous;
            ev->queue=NULL;ev->next=NULL;
        }
    }
    ble_npl_hw_exit_critical(flags);
}
bool ble_npl_eventq_is_empty(struct ble_npl_eventq *q) {
    uint32_t flags=ble_npl_hw_enter_critical();bool empty=!q||!q->head;
    ble_npl_hw_exit_critical(flags);return empty;
}
void ble_npl_eventq_deinit(struct ble_npl_eventq *q) {
    uint32_t flags=ble_npl_hw_enter_critical();
    while(q->head) {struct ble_npl_event *ev=q->head;q->head=ev->next;ev->queue=NULL;ev->next=NULL;}
    q->tail=NULL;q->initialized=false;ble_npl_hw_exit_critical(flags);
}
static void npl_service_callouts(void) {
    for(unsigned i=0;i<MEOS_NPL_CALLOUTS;i++) {
        uint32_t flags=ble_npl_hw_enter_critical();struct ble_npl_callout *co=npl_callouts[i];
        if(!co||!co->active||!expired(ble_npl_time_get(),co->deadline)) {
            ble_npl_hw_exit_critical(flags);continue;
        }
        co->active=false;
        if(co->queue)ble_npl_eventq_put(co->queue,&co->event);
        ble_npl_hw_exit_critical(flags);
        if(!co->queue)ble_npl_event_run(&co->event);
    }
}
struct ble_npl_event *ble_npl_eventq_get(struct ble_npl_eventq *q,ble_npl_time_t timeout) {
    if(!arch_irq_enabled())return NULL;
    uint32_t start=ble_npl_time_get();
    for(;;) {
        npl_service_callouts();uint32_t flags=ble_npl_hw_enter_critical();
        struct ble_npl_event *ev=q?q->head:NULL;
        if(ev) {
            q->head=ev->next;if(!q->head)q->tail=NULL;ev->next=NULL;ev->queue=NULL;
        }
        ble_npl_hw_exit_critical(flags);
        if(ev||!q||!q->initialized||timed_out(start,timeout))return ev;
        task_sleep(1);
    }
}
ble_npl_error_t ble_npl_mutex_init(struct ble_npl_mutex *m) {memset(m,0,sizeof *m);m->initialized=true;return BLE_NPL_OK;}
ble_npl_error_t ble_npl_mutex_pend(struct ble_npl_mutex *m,ble_npl_time_t timeout) {
    if(!m||!m->initialized)return BLE_NPL_BAD_MUTEX;
    if(!arch_irq_enabled())return BLE_NPL_ERR_IN_ISR;
    void *task=ble_npl_get_current_task_id();if(!task)return BLE_NPL_OS_NOT_STARTED;
    uint32_t start=ble_npl_time_get();
    for(;;) {
        uint32_t flags=ble_npl_hw_enter_critical();
        if(!m->owner||m->owner==task) {
            if(m->depth==UINT16_MAX) {ble_npl_hw_exit_critical(flags);return BLE_NPL_EBUSY;}
            m->owner=task;m->depth++;ble_npl_hw_exit_critical(flags);return BLE_NPL_OK;
        }
        ble_npl_hw_exit_critical(flags);
        if(timed_out(start,timeout))return BLE_NPL_TIMEOUT;
        npl_service_callouts();task_sleep(1);
    }
}
ble_npl_error_t ble_npl_mutex_release(struct ble_npl_mutex *m) {
    if(!m||!m->initialized)return BLE_NPL_BAD_MUTEX;
    uint32_t flags=ble_npl_hw_enter_critical();
    if(!m->depth||m->owner!=ble_npl_get_current_task_id()) {ble_npl_hw_exit_critical(flags);return BLE_NPL_BAD_MUTEX;}
    if(!--m->depth)m->owner=NULL;
    ble_npl_hw_exit_critical(flags);return BLE_NPL_OK;
}
ble_npl_error_t ble_npl_mutex_deinit(struct ble_npl_mutex *m) {
    if(!m||!m->initialized)return BLE_NPL_BAD_MUTEX;
    if(m->depth)return BLE_NPL_EBUSY;
    m->initialized=false;return BLE_NPL_OK;
}
ble_npl_error_t ble_npl_sem_init(struct ble_npl_sem *s,uint16_t tokens) {s->tokens=tokens;s->initialized=true;return BLE_NPL_OK;}
ble_npl_error_t ble_npl_sem_pend(struct ble_npl_sem *s,ble_npl_time_t timeout) {
    if(!s||!s->initialized)return BLE_NPL_INVALID_PARAM;
    if(!arch_irq_enabled())return BLE_NPL_ERR_IN_ISR;
    uint32_t start=ble_npl_time_get();
    for(;;) {
        uint32_t flags=ble_npl_hw_enter_critical();
        if(s->tokens) {s->tokens--;ble_npl_hw_exit_critical(flags);return BLE_NPL_OK;}
        ble_npl_hw_exit_critical(flags);
        if(timed_out(start,timeout))return BLE_NPL_TIMEOUT;
        npl_service_callouts();task_sleep(1);
    }
}
ble_npl_error_t ble_npl_sem_release(struct ble_npl_sem *s) {
    if(!s||!s->initialized)return BLE_NPL_INVALID_PARAM;
    uint32_t flags=ble_npl_hw_enter_critical();
    if(s->tokens==UINT16_MAX) {ble_npl_hw_exit_critical(flags);return BLE_NPL_EBUSY;}
    s->tokens++;ble_npl_hw_exit_critical(flags);return BLE_NPL_OK;
}
ble_npl_error_t ble_npl_sem_deinit(struct ble_npl_sem *s) {if(!s)return BLE_NPL_INVALID_PARAM;s->initialized=false;return BLE_NPL_OK;}
uint16_t ble_npl_sem_get_count(struct ble_npl_sem *s) {return s->tokens;}
void ble_npl_callout_init(struct ble_npl_callout *co,struct ble_npl_eventq *q,ble_npl_event_fn *fn,void *arg) {
    assert(co&&fn);
    uint32_t flags=ble_npl_hw_enter_critical();unsigned slot=MEOS_NPL_CALLOUTS;
    for(unsigned i=0;i<MEOS_NPL_CALLOUTS;i++) {
        if(npl_callouts[i]==co) {slot=i;break;}
        if(!npl_callouts[i]&&slot==MEOS_NPL_CALLOUTS)slot=i;
    }
    if(slot==MEOS_NPL_CALLOUTS) {
        memset(co,0,sizeof *co);ble_npl_hw_exit_critical(flags);return;
    }
    if(npl_callouts[slot]==co&&co->event.queue)ble_npl_eventq_remove(co->event.queue,&co->event);
    memset(co,0,sizeof *co);ble_npl_event_init(&co->event,fn,arg);co->queue=q;co->registered=true;npl_callouts[slot]=co;
    ble_npl_hw_exit_critical(flags);
}
ble_npl_error_t ble_npl_callout_reset(struct ble_npl_callout *co,ble_npl_time_t ticks) {
    if(!co||ticks>INT32_MAX)return BLE_NPL_INVALID_PARAM;
    if(!co->registered)return BLE_NPL_ENOMEM;
    uint32_t flags=ble_npl_hw_enter_critical();
    if(co->event.queue)ble_npl_eventq_remove(co->event.queue,&co->event);
    co->deadline=ble_npl_time_get()+(ticks?ticks:1);co->active=true;
    ble_npl_hw_exit_critical(flags);return BLE_NPL_OK;
}
void ble_npl_callout_stop(struct ble_npl_callout *co) {
    uint32_t flags=ble_npl_hw_enter_critical();co->active=false;
    if(co->event.queue)ble_npl_eventq_remove(co->event.queue,&co->event);
    ble_npl_hw_exit_critical(flags);
}
bool ble_npl_callout_is_active(struct ble_npl_callout *co) {return co->active;}
ble_npl_time_t ble_npl_callout_get_ticks(struct ble_npl_callout *co) {return co->deadline;}
ble_npl_time_t ble_npl_callout_remaining_ticks(struct ble_npl_callout *co,ble_npl_time_t now) {
    return co->active&&!expired(now,co->deadline)?co->deadline-now:0;
}
void ble_npl_callout_set_arg(struct ble_npl_callout *co,void *arg) {co->event.arg=arg;}
