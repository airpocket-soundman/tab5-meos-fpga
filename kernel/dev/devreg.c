#include <m5os/devreg.h>
#include <m5os/arch.h>
#include <m5os/errno.h>
#include <string.h>

#define DEVREG_MAX       16
#define DEVREG_LISTENERS 4

static devreg_entry_t s_entries[DEVREG_MAX];
static bool           s_used[DEVREG_MAX];
static struct { devreg_listener_t fn; void *ctx; } s_listeners[DEVREG_LISTENERS];

static int find_index(const char *name)
{
    for (int i = 0; i < DEVREG_MAX; i++)
        if (s_used[i] && strcmp(s_entries[i].name, name) == 0) return i;
    return -1;
}

static void notify(devreg_event_t ev, const devreg_entry_t *e)
{
    for (int i = 0; i < DEVREG_LISTENERS; i++) {
        devreg_listener_t fn = s_listeners[i].fn;
        if (fn) fn(ev, e, s_listeners[i].ctx);
    }
}

int devreg_add(devreg_class_t cls, const char *name, const char *kind, unsigned unit, void *obj)
{
    if (!name || !name[0] || strlen(name) >= DEVREG_NAME_MAX || !kind) return ERR_INVAL;
    if (cls != DEVREG_BLOCK && cls != DEVREG_CAMERA) return ERR_INVAL;
    if (cls == DEVREG_BLOCK && !obj) return ERR_INVAL;
    devreg_entry_t copy;
    uint32_t irq = arch_irq_disable();
    int slot = -1;
    for (int i = 0; i < DEVREG_MAX; i++) {
        if (s_used[i] && strcmp(s_entries[i].name, name) == 0) { slot = -2; break; }
        if (!s_used[i] && slot == -1) slot = i;
    }
    if (slot >= 0) {
        devreg_entry_t *e = &s_entries[slot];
        memset(e, 0, sizeof *e);
        strcpy(e->name, name);
        e->cls = cls;
        e->kind = kind;
        e->unit = unit;
        e->obj = obj;
        s_used[slot] = true;
        copy = *e;
    }
    arch_irq_restore(irq);
    if (slot < 0) return ERR_BUSY;
    notify(DEVREG_EVENT_ADD, &copy);
    return ERR_OK;
}

int devreg_remove(const char *name)
{
    if (!name) return ERR_INVAL;
    devreg_entry_t copy;
    uint32_t irq = arch_irq_disable();
    int i = find_index(name);
    if (i >= 0) copy = s_entries[i];
    arch_irq_restore(irq);
    if (i < 0) return ERR_NOENT;
    notify(DEVREG_EVENT_REMOVE, &copy);          /* before the entry goes: listeners may still look it up */
    irq = arch_irq_disable();
    i = find_index(name);
    if (i >= 0) s_used[i] = false;
    arch_irq_restore(irq);
    return ERR_OK;
}

bool devreg_find(const char *name, devreg_entry_t *out)
{
    if (!name) return false;
    uint32_t irq = arch_irq_disable();
    int i = find_index(name);
    if (i >= 0 && out) *out = s_entries[i];
    arch_irq_restore(irq);
    return i >= 0;
}

int devreg_foreach(int (*cb)(const devreg_entry_t *, void *), void *ctx)
{
    int seen = 0;
    for (int i = 0; i < DEVREG_MAX; i++) {
        devreg_entry_t copy;
        uint32_t irq = arch_irq_disable();
        bool used = s_used[i];
        if (used) copy = s_entries[i];
        arch_irq_restore(irq);
        if (!used) continue;
        seen++;
        if (cb(&copy, ctx)) break;
    }
    return seen;
}

int devreg_subscribe(devreg_listener_t fn, void *ctx)
{
    if (!fn) return ERR_INVAL;
    uint32_t irq = arch_irq_disable();
    int slot = -1;
    for (int i = 0; i < DEVREG_LISTENERS; i++) {
        if (s_listeners[i].fn == fn && s_listeners[i].ctx == ctx) { slot = -2; break; }
        if (!s_listeners[i].fn && slot == -1) slot = i;
    }
    if (slot >= 0) { s_listeners[slot].fn = fn; s_listeners[slot].ctx = ctx; }
    arch_irq_restore(irq);
    if (slot == -2) return ERR_OK;               /* already subscribed */
    if (slot < 0) return ERR_BUSY;
    for (int i = 0; i < DEVREG_MAX; i++) {       /* tell the new listener what is already there */
        devreg_entry_t copy;
        irq = arch_irq_disable();
        bool used = s_used[i];
        if (used) copy = s_entries[i];
        arch_irq_restore(irq);
        if (used) fn(DEVREG_EVENT_ADD, &copy, ctx);
    }
    return ERR_OK;
}

void devreg_set_mount(const char *name, const char *mount)
{
    if (!name) return;
    uint32_t irq = arch_irq_disable();
    int i = find_index(name);
    if (i >= 0) {
        size_t n = mount ? strlen(mount) : 0;
        if (n >= DEVREG_MOUNT_MAX) n = DEVREG_MOUNT_MAX - 1;
        if (n) memcpy(s_entries[i].mount, mount, n);
        s_entries[i].mount[n] = 0;
    }
    arch_irq_restore(irq);
}
