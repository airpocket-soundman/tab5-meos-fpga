/* IPC self test. */
#include <m5os/msg.h>
#include <m5os/m5ex.h>
#include <m5os/app.h>
#include <m5os/task.h>
#include <m5os/errno.h>
#include <m5os/hal_time.h>
#include <m5os/kmalloc.h>
#include <m5os/kprintf.h>

#define CK(c) do { if (!(c)) { kprintf("  FAIL line %d: %s\n", __LINE__, #c); ok = false; } } while (0)
#define SETTLE_MS 250u

static uint32_t now_ms(void) { return (uint32_t)(hal_time_now_us() / 1000u); }

static msg_t mk(uint32_t type, uint32_t tag)
{
    msg_t m = { .type = type };
    m.data[0] = (uint8_t)tag; m.data[1] = (uint8_t)(tag >> 8);
    return m;
}
static bool streq_n(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static uint32_t tag_of(const msg_t *m) { return m->data[0] | (uint32_t)m->data[1] << 8; }

/* ---- helper tasks ----------------------------------------------------------- */

static volatile uint32_t s_got[MSG_QUEUE_DEPTH + 2];
static volatile int s_got_n;

/* Receiver that starts reading only after a delay, so senders find a full mailbox. */
static void slow_reader(void *arg)
{
    task_sleep((uint32_t)(uintptr_t)arg);
    for (;;) {
        msg_t m;
        if (msg_receive_timeout(&m, 300) != ERR_OK) return;
        if (s_got_n < (int)(sizeof s_got / sizeof s_got[0])) s_got[s_got_n++] = tag_of(&m);
    }
}

static volatile uint32_t s_target_pid;
static volatile int s_blocked_send_rc = 12345;

static void sender_to_dead(void *arg)                            /* blocks on a full mailbox, then its owner is killed */
{
    (void)arg;
    msg_t m = mk(9, 9);
    s_blocked_send_rc = msg_send_timeout(s_target_pid, &m, MSG_WAIT_FOREVER);
}

static void sleeper(void *arg) { (void)arg; for (;;) task_sleep(1000); }

static void late_sender(void *arg)                               /* sends to the test task after a delay */
{
    uint32_t to = (uint32_t)(uintptr_t)arg;
    task_sleep(40);
    msg_t m = mk(5, 777);
    msg_send(to, &m);
}

static bool app_info_of(uint32_t id, app_info_t *out)
{
    app_info_t all[APP_MAX];
    int n = app_list(all, APP_MAX);
    for (int i = 0; i < n; i++) if (all[i].app_id == id) { *out = all[i]; return true; }
    return false;
}

bool msg_selftest(void)
{
    bool ok = true;
    kmem_stats_t base, now;
    uint32_t me = task_current()->id;
    msg_t m, r;
    /* The boot-time demo writers (logA/logB) share the CPU with these tests and may still be
     * running; their stacks would make the "nothing leaked" baseline wrong.  Wait for them. */
    for (int i = 0; i < 600 && (msg_lookup("logA") > 0 || msg_lookup("logB") > 0); i++) task_sleep(50);
    task_sleep(SETTLE_MS);                                        /* ... and for ended tasks to be reclaimed */
    msg_try_receive(&r);                                          /* creates OUR mailbox, which lives until this task ends */
    kmem_internal_stats(&base);                                   /* baseline taken after it: everything else must come back */

    /* ---- basics ------------------------------------------------------------ */
    CK(msg_send(9999, &(msg_t){ 0 }) == ERR_NOENT);
    CK(msg_try_receive(&r) == ERR_NOMSG);
    m = mk(0x1234, 0xBEEF);
    CK(msg_send(me, &m) == ERR_OK);                               /* to ourselves: a loopback */
    CK(msg_try_receive(&r) == ERR_OK && r.sender == me && r.type == 0x1234 && tag_of(&r) == 0xBEEF);
    CK(msg_try_receive(&r) == ERR_NOMSG);
    m = mk(1, 1); m.sender = 4242;
    CK(msg_send(me, &m) == ERR_OK && msg_try_receive(&r) == ERR_OK && r.sender == me);   /* sender cannot be forged */

    /* ---- FIFO and capacity ------------------------------------------------ */
    for (uint32_t i = 0; i < MSG_QUEUE_DEPTH; i++) { m = mk(2, 100 + i); CK(msg_send(me, &m) == ERR_OK); }
    m = mk(2, 999);
    CK(msg_send(me, &m) == ERR_BUSY);                             /* full: send never waits */
    uint32_t t0 = now_ms();
    CK(msg_send_timeout(me, &m, 60) == ERR_TIMEOUT);              /* full: bounded wait */
    uint32_t waited = now_ms() - t0;
    CK(waited >= 55 && waited < 200);
    for (uint32_t i = 0; i < MSG_QUEUE_DEPTH; i++) CK(msg_try_receive(&r) == ERR_OK && tag_of(&r) == 100 + i);   /* in order */
    CK(msg_try_receive(&r) == ERR_NOMSG);

    /* ---- blocking receive -------------------------------------------------- */
    t0 = now_ms();
    CK(msg_receive_timeout(&r, 50) == ERR_TIMEOUT);
    waited = now_ms() - t0;
    CK(waited >= 45 && waited < 200);                             /* really waited, and woke on time */

    CK(task_create("late-sender", late_sender, (void *)(uintptr_t)me, TASK_PRIO_DEFAULT) > 0);
    t0 = now_ms();
    CK(msg_receive_timeout(&r, 1000) == ERR_OK && tag_of(&r) == 777 && r.type == 5);
    waited = now_ms() - t0;
    CK(waited >= 30 && waited < 150);                             /* woken by the send, not by the deadline */

    /* ---- a blocked sender is released when room appears ---------------------- */
    s_got_n = 0;
    int reader = task_create("slow-reader", slow_reader, (void *)(uintptr_t)120, TASK_PRIO_DEFAULT);
    CK(reader > 0);
    t0 = now_ms();
    for (uint32_t i = 0; i < MSG_QUEUE_DEPTH + 1; i++) {          /* one more than fits: the last send must wait for the reader */
        m = mk(3, 200 + i);
        CK(msg_send_timeout((uint32_t)reader, &m, 1000) == ERR_OK);
    }
    waited = now_ms() - t0;
    CK(waited >= 90);                                             /* it did have to wait for the reader to start */
    task_sleep(150);
    CK(s_got_n == MSG_QUEUE_DEPTH + 1);
    for (int i = 0; i < s_got_n && i < MSG_QUEUE_DEPTH + 1; i++) CK(s_got[i] == 200u + (uint32_t)i);   /* order preserved across the wait */
    task_sleep(400);                                              /* reader gives up after its own 300 ms timeout */

    /* ---- the other side dies while we wait ------------------------------------ */
    int victim = task_create("victim", sleeper, NULL, TASK_PRIO_DEFAULT);
    CK(victim > 0);
    for (uint32_t i = 0; i < MSG_QUEUE_DEPTH; i++) { m = mk(4, i); CK(msg_send((uint32_t)victim, &m) == ERR_OK); }
    s_target_pid = (uint32_t)victim;
    s_blocked_send_rc = 12345;
    CK(task_create("blocked-sender", sender_to_dead, NULL, TASK_PRIO_DEFAULT) > 0);
    task_sleep(80);
    CK(s_blocked_send_rc == 12345);                               /* still blocked on the full mailbox */
    CK(task_kill((uint32_t)victim) == ERR_OK);
    task_sleep(100);
    CK(s_blocked_send_rc == ERR_NOENT);                           /* released with an error, not hung */
    m = mk(4, 0);
    CK(msg_send((uint32_t)victim, &m) == ERR_NOENT);

    /* ---- lookup and echo servers (kernel task and LOADED code) ----------------- */
    CK(msg_lookup("no-such-task") == ERR_NOENT);
    uint32_t id;
    CK(app_start("echod", &id) == ERR_OK);
    task_sleep(60);
    int echod = msg_lookup("echod");
    CK(echod > 0);
    m = (msg_t){ .type = 7 };
    for (int i = 0; i < 6; i++) m.data[i] = (uint8_t)"hello"[i];                   /* text from data[0], NUL-terminated */
    CK(msg_send((uint32_t)echod, &m) == ERR_OK);
    CK(msg_receive_timeout(&r, 1000) == ERR_OK && r.sender == (uint32_t)echod && r.type == (7u | 0x8000u));
    CK(streq_n((const char *)r.data, "echo:hello"));
    m = mk(1, 0); m.data[0] = 'q'; m.data[1] = 'u'; m.data[2] = 'i'; m.data[3] = 't'; m.data[4] = 0;
    CK(msg_send((uint32_t)echod, &m) == ERR_OK);
    task_sleep(SETTLE_MS);
    app_info_t info;
    CK(app_info_of(id, &info) && info.state == APP_EXITED);

    CK(app_start_file("/apps/m5echo.m5ex", &id) == ERR_OK);
    task_sleep(SETTLE_MS);
    int m5echo = msg_lookup("m5echo");
    CK(m5echo > 0);
    m = (msg_t){ .type = 8 };
    m.data[0] = 'o'; m.data[1] = 'k'; m.data[2] = 0;
    CK(msg_send((uint32_t)m5echo, &m) == ERR_OK);
    CK(msg_receive_timeout(&r, 1000) == ERR_OK && r.sender == (uint32_t)m5echo && r.type == (8u | 0x8000u));
    CK(streq_n((const char *)r.data, "m5echo:ok"));                 /* built by LOADED code, delivered through the kernel */
    CK(app_stop(id) == ERR_OK);
    task_sleep(SETTLE_MS);
    CK(app_info_of(id, &info) && info.state == APP_KILLED && info.mem_bytes == 0);

    /* ---- nothing left behind: mailboxes are freed with their tasks ------------- */
    while (msg_try_receive(&r) == ERR_OK) { }
    task_sleep(SETTLE_MS);
    kmem_internal_stats(&now);
    CK(now.used == base.used);                                    /* exact: every other task's mailbox and stack came back */
    if (now.used != base.used) kprintf("  heap used changed by %d bytes (base %u, now %u)\n", (int)(now.used - base.used), (uint32_t)base.used, (uint32_t)now.used);
    kprintf("msg selftest: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}
