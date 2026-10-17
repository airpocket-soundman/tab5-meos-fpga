#include <m5os/msg.h>
#include <m5os/app.h>
#include <m5os/arch.h>
#include <m5os/errno.h>
#include <m5os/hal_time.h>
#include <m5os/kmalloc.h>
#include "../scheduler/sched_internal.h"

#define IDLE_PID    1u
#define KIND_RECV   1
#define KIND_SEND   2

typedef struct mailbox {
    msg_t   slot[MSG_QUEUE_DEPTH];
    uint8_t head;               /* index of the oldest message */
    uint8_t count;
    bool    dead;               /* owner has ended: nothing more may be queued */
} mailbox_t;

static bool name_eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

/* ---------------------------------------------------------------- helpers
 * Everything below that touches a mailbox or a task's state runs with interrupts masked. */

static task_t *find_live(uint32_t pid)
{
    for (int i = 0; i < TASK_MAX; i++) {
        task_t *t = &g_tasks[i];
        if (t->id == pid && t->state != TASK_UNUSED && t->state != TASK_TERMINATED) return t;
    }
    return NULL;
}

/* Wake the best (highest-priority) task blocked on `mb` for `kind`; returns it or NULL. */
static task_t *wake_one(mailbox_t *mb, int kind)
{
    task_t *best = NULL;
    for (int i = 0; i < TASK_MAX; i++) {
        task_t *t = &g_tasks[i];
        if (t->state == TASK_BLOCKED && t->wait_obj == mb && t->wait_kind == kind && (!best || t->priority > best->priority)) best = t;
    }
    if (best) { best->wait_obj = NULL; best->wait_kind = 0; best->state = TASK_RUNNABLE; }
    return best;
}

/* Create `t`'s mailbox if it has none.  The allocation happens before masking interrupts. */
static mailbox_t *ensure_mailbox(uint32_t pid)
{
    mailbox_t *fresh = NULL;
    for (;;) {
        uint32_t irq = arch_irq_disable();
        task_t *t = find_live(pid);
        mailbox_t *mb = t ? t->mbox : NULL;
        if (t && !mb && fresh) { fresh->head = fresh->count = 0; fresh->dead = false; t->mbox = mb = fresh; fresh = NULL; }
        arch_irq_restore(irq);
        if (!t) { kfree(fresh); return NULL; }
        if (mb) { kfree(fresh); return mb; }
        fresh = kmalloc_internal(sizeof(mailbox_t));
        if (!fresh) return NULL;
    }
}

/* Block the calling task on `mb`.  Interrupts are masked on entry; they stay masked until the
 * caller restores them.  Returns true if the wait ended because the deadline passed. */
static bool block_on(task_t *self, mailbox_t *mb, int kind, uint32_t timeout_ms)
{
    self->wait_obj = mb;
    self->wait_kind = (uint8_t)kind;
    self->wait_timed_out = false;
    self->wait_timed = timeout_ms != MSG_WAIT_FOREVER;
    if (self->wait_timed) self->wakeup_us = hal_time_now_us() + (uint64_t)timeout_ms * 1000u;
    self->state = TASK_BLOCKED;
    sched_reschedule();                                   /* back here when woken by the other side or by the deadline */
    bool timed_out = self->wait_timed_out;
    self->wait_timed = false;
    self->wait_timed_out = false;
    return timed_out;
}

/* ------------------------------------------------------------------ send */

int msg_send_timeout(uint32_t pid, const msg_t *in, uint32_t timeout_ms)
{
    if (!task_has_perm(APP_PERM_IPC)) return ERR_PERM;
    if (!in) return ERR_INVAL;
    task_t *self = task_current();

    for (;;) {
        if (!ensure_mailbox(pid)) return find_live(pid) ? ERR_NOMEM : ERR_NOENT;

        uint32_t irq = arch_irq_disable();
        task_t *t = find_live(pid);
        mailbox_t *mb = t ? t->mbox : NULL;
        if (!mb || mb->dead) { arch_irq_restore(irq); return ERR_NOENT; }

        if (mb->count < MSG_QUEUE_DEPTH) {
            msg_t *slot = &mb->slot[(mb->head + mb->count) % MSG_QUEUE_DEPTH];
            *slot = *in;
            slot->sender = self->id;                      /* stamped by the kernel */
            mb->count++;
            task_t *woken = wake_one(mb, KIND_RECV);
            bool preempt = woken && woken->priority > self->priority;
            arch_irq_restore(irq);
            if (preempt) task_yield();                    /* let the urgent receiver run now, not at the next tick */
            return ERR_OK;
        }
        if (timeout_ms == 0) { arch_irq_restore(irq); return ERR_BUSY; }
        if (self->id == IDLE_PID) { arch_irq_restore(irq); return ERR_INVAL; }

        bool timed_out = block_on(self, mb, KIND_SEND, timeout_ms);
        arch_irq_restore(irq);
        if (timed_out) return ERR_TIMEOUT;
        /* woken because there is room (or the owner died): look again */
    }
}

int msg_send(uint32_t pid, const msg_t *m) { return msg_send_timeout(pid, m, 0); }

/* --------------------------------------------------------------- receive */

int msg_receive_timeout(msg_t *out, uint32_t timeout_ms)
{
    if (!task_has_perm(APP_PERM_IPC)) return ERR_PERM;
    if (!out) return ERR_INVAL;
    task_t *self = task_current();
    if (!ensure_mailbox(self->id)) return ERR_NOMEM;

    for (;;) {
        uint32_t irq = arch_irq_disable();
        mailbox_t *mb = self->mbox;
        if (mb->count > 0) {
            *out = mb->slot[mb->head];
            mb->head = (uint8_t)((mb->head + 1) % MSG_QUEUE_DEPTH);
            mb->count--;
            task_t *woken = wake_one(mb, KIND_SEND);      /* a sender may have been waiting for room */
            bool preempt = woken && woken->priority > self->priority;
            arch_irq_restore(irq);
            if (preempt) task_yield();
            return ERR_OK;
        }
        if (timeout_ms == 0) { arch_irq_restore(irq); return ERR_NOMSG; }
        if (self->id == IDLE_PID) { arch_irq_restore(irq); return ERR_INVAL; }

        bool timed_out = block_on(self, mb, KIND_RECV, timeout_ms);
        arch_irq_restore(irq);
        if (timed_out) return ERR_TIMEOUT;
    }
}

int msg_receive(msg_t *m)     { return msg_receive_timeout(m, MSG_WAIT_FOREVER); }
int msg_try_receive(msg_t *m) { return msg_receive_timeout(m, 0); }

/* ---------------------------------------------------------------- lookup */

int msg_lookup(const char *name)
{
    if (!task_has_perm(APP_PERM_IPC)) return ERR_PERM;
    if (!name) return ERR_INVAL;
    int pid = ERR_NOENT;
    uint32_t irq = arch_irq_disable();
    for (int i = 0; i < TASK_MAX; i++) {
        const task_t *t = &g_tasks[i];
        if (t->state != TASK_UNUSED && t->state != TASK_TERMINATED && name_eq(t->name, name)) { pid = (int)t->id; break; }
    }
    arch_irq_restore(irq);
    return pid;
}

/* ------------------------------------------------------- task life cycle */

/* The task is ending (exit or kill): refuse new mail and release senders blocked on its full
 * mailbox, so they get ERR_NOENT instead of waiting for a receiver that will never come. */
void msg_task_ended(task_t *t)
{
    if (!t->mbox) return;
    t->mbox->dead = true;
    while (wake_one(t->mbox, KIND_SEND)) { }
}

void msg_free_mailbox(task_t *t)
{
    mailbox_t *mb = t->mbox;
    t->mbox = NULL;
    kfree(mb);
}
