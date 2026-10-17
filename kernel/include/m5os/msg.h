#pragma once
#include <m5os/types.h>

/*
 * Inter-task messages: a fixed-size message and one mailbox per task.
 *
 *     msg_t m = { .type = 1 };  copy text into m.data ...
 *     msg_send(pid, &m);                  // never waits: ERR_BUSY if the mailbox is full
 *     msg_receive(&m);                    // waits (BLOCKED) until a message arrives
 *     msg_try_receive(&m);                // ERR_NOMSG if there is none
 *
 * The mailbox holds MSG_QUEUE_DEPTH messages, delivered in order (FIFO).  It is created the
 * first time a task sends to, or receives from, it and freed when the task is reclaimed.
 * Delivery copies the message, and the kernel stamps `sender` - it cannot be forged.
 *
 * Rules: task context only (never from an interrupt handler or with interrupts masked);
 * the idle task (pid 1) may not wait (ERR_INVAL); applications need APP_PERM_IPC.
 * Waiting is safe against the other side disappearing: a sender blocked on a full mailbox
 * whose owner is killed or exits gets ERR_NOENT, never a hang.
 */
#define MSG_DATA_SIZE     24
#define MSG_QUEUE_DEPTH   8
#define MSG_WAIT_FOREVER  0xFFFFFFFFu

typedef struct {
    uint32_t sender;          /* pid of the sender, filled in by the kernel */
    uint32_t type;            /* application-defined */
    uint8_t  data[MSG_DATA_SIZE];
} msg_t;

int msg_send(uint32_t pid, const msg_t *m);             /* 0, ERR_NOENT, ERR_BUSY (full), ERR_PERM, ERR_NOMEM */
int msg_receive(msg_t *m);                               /* 0, or ERR_PERM / ERR_INVAL */
int msg_try_receive(msg_t *m);                           /* 0 or ERR_NOMSG */

/* timeout_ms: 0 = do not wait, MSG_WAIT_FOREVER = no deadline.  A deadline that passes gives ERR_TIMEOUT. */
int msg_send_timeout(uint32_t pid, const msg_t *m, uint32_t timeout_ms);
int msg_receive_timeout(msg_t *m, uint32_t timeout_ms);

int msg_lookup(const char *task_name);                   /* pid of a live task/app with that name, or ERR_NOENT */

/* Kernel-internal (scheduler): */
struct task;
void msg_task_ended(struct task *t);                     /* interrupts masked: wake senders blocked on t's mailbox */
void msg_free_mailbox(struct task *t);                   /* task is being reclaimed */

bool msg_selftest(void);
