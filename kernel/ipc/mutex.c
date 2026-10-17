#include <m5os/mutex.h>
#include <m5os/arch.h>
#include <m5os/kernel.h>
#include "../scheduler/sched_internal.h"

#define IDLE_PID  1u

void mutex_init(mutex_t *m) { m->owner = NULL; m->next_held = NULL; }

/* Every owned mutex is linked into its owner's `held` list, so that killing a
 * task can release them (otherwise a task killed while holding e.g. the
 * console lock would freeze every later kprintf). */
static void held_push(task_t *t, mutex_t *m) { m->owner = t; m->next_held = t->held; t->held = m; }

static void held_remove(task_t *t, mutex_t *m)
{
    for (mutex_t **pp = &t->held; *pp; pp = &(*pp)->next_held) {
        if (*pp == m) { *pp = m->next_held; break; }
    }
    m->next_held = NULL;
}

/* Give `m` (owned by `owner`) to the best waiter, or free it.  Returns the task
 * that received it (NULL if none).  Interrupts must be masked. */
static task_t *release_locked(mutex_t *m, task_t *owner)
{
    held_remove(owner, m);
    task_t *best = NULL;
    for (int i = 0; i < TASK_MAX; i++) {
        task_t *t = &g_tasks[i];
        if (t->state == TASK_BLOCKED && t->wait_obj == m && (best == NULL || t->priority > best->priority)) best = t;
    }
    if (best) {
        held_push(best, m);
        best->wait_obj = NULL;
        best->state = TASK_RUNNABLE;
    } else {
        m->owner = NULL;
    }
    return best;
}

bool mutex_trylock(mutex_t *m)
{
    uint32_t irq = arch_irq_disable();
    bool got = (m->owner == NULL);
    if (got) held_push(task_current(), m);
    arch_irq_restore(irq);
    return got;
}

void mutex_lock(mutex_t *m)
{
    task_t *self = task_current();

    if (self->id == IDLE_PID) {                 /* idle may not block: poll */
        while (!mutex_trylock(m)) task_yield();
        return;
    }

    uint32_t irq = arch_irq_disable();
    if (m->owner == self) kernel_panic("mutex_lock: recursive lock");
    if (m->owner == NULL) {
        held_push(self, m);
    } else {
        self->wait_obj = m;
        self->state = TASK_BLOCKED;
        sched_reschedule();                     /* returns once unlock() handed the mutex to us */
        if (m->owner != self) kernel_panic("mutex_lock: woken without ownership");
    }
    arch_irq_restore(irq);
}

void mutex_unlock(mutex_t *m)
{
    uint32_t irq = arch_irq_disable();
    task_t *self = task_current();
    if (m->owner != self) kernel_panic("mutex_unlock: not the owner");
    task_t *next = release_locked(m, self);
    bool preempt = next && next->priority > self->priority;
    arch_irq_restore(irq);

    if (preempt) task_yield();                  /* do not wait for the next tick */
}

void mutex_release_all(task_t *t)
{
    while (t->held) release_locked(t->held, t);
}
