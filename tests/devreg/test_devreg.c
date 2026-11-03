/* Host test of kernel/dev/devreg.c: add / find / remove, listeners, limits. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <m5os/devreg.h>
#include <m5os/errno.h>

/* The registry masks interrupts around its table; on the host that is nothing. */
uint32_t arch_irq_disable(void) { return 0; }
void arch_irq_restore(uint32_t flags) { (void)flags; }

static int events, last_event;
static char last_name[DEVREG_NAME_MAX], log_buf[256];
static void listener(devreg_event_t ev, const devreg_entry_t *e, void *ctx) {
    (void)ctx;
    events++;
    last_event = ev;
    strcpy(last_name, e->name);
    /* on removal the entry is still there */
    devreg_entry_t copy;
    if (ev == DEVREG_EVENT_REMOVE)
        assert(devreg_find(e->name, &copy));
    strcat(log_buf, ev == DEVREG_EVENT_ADD ? "+" : "-");
    strcat(log_buf, e->name);
    strcat(log_buf, " ");
}
static void listener2(devreg_event_t ev, const devreg_entry_t *e, void *ctx) { (void)ev; (void)e; (*(int *)ctx)++; }

static int count_cb(const devreg_entry_t *e, void *ctx) { (void)e; return ++*(int *)ctx >= 2; }
static int names_cb(const devreg_entry_t *e, void *ctx) { strcat((char *)ctx, e->name); strcat((char *)ctx, ","); return 0; }

int main(void) {
    int sd_dev = 0, usb_dev = 0;
    devreg_entry_t e;

    assert(devreg_add(DEVREG_BLOCK, "sd0", "sd", 0, &sd_dev) == ERR_OK);
    assert(devreg_find("sd0", &e) && e.cls == DEVREG_BLOCK && !strcmp(e.kind, "sd") && e.obj == &sd_dev && e.unit == 0);
    assert(e.mount[0] == 0);
    assert(devreg_add(DEVREG_BLOCK, "sd0", "sd", 0, &sd_dev) == ERR_BUSY);          /* name taken */
    assert(devreg_add(DEVREG_BLOCK, "", "sd", 0, &sd_dev) == ERR_INVAL);
    assert(devreg_add(DEVREG_BLOCK, "this-name-is-way-too-long", "sd", 0, &sd_dev) == ERR_INVAL);
    assert(devreg_add(DEVREG_BLOCK, "x", "sd", 0, NULL) == ERR_INVAL);              /* a block device needs its object */
    assert(devreg_add((devreg_class_t)9, "x", "sd", 0, &sd_dev) == ERR_INVAL);
    assert(devreg_add(DEVREG_CAMERA, "camera0", "onboard", 0, DEVREG_CAMERA_ONBOARD) == ERR_OK);
    assert(devreg_find("camera0", &e) && e.cls == DEVREG_CAMERA && e.obj == DEVREG_CAMERA_ONBOARD);
    assert(!devreg_find("nothing", &e) && !devreg_find(NULL, &e));

    /* a listener hears about what is already there, then about changes */
    assert(devreg_subscribe(listener, NULL) == ERR_OK);
    assert(events == 2 && !strcmp(log_buf, "+sd0 +camera0 "));
    assert(devreg_subscribe(listener, NULL) == ERR_OK && events == 2);              /* same listener twice: ignored */
    assert(devreg_add(DEVREG_BLOCK, "usb1", "usb", 1, &usb_dev) == ERR_OK);
    assert(events == 3 && last_event == DEVREG_EVENT_ADD && !strcmp(last_name, "usb1"));
    devreg_set_mount("usb1", "/usb1");
    assert(devreg_find("usb1", &e) && !strcmp(e.mount, "/usb1"));
    devreg_set_mount("usb1", "");
    assert(devreg_find("usb1", &e) && e.mount[0] == 0);
    devreg_set_mount("usb1", "/a-mount-path-much-longer-than-the-field-allows");
    assert(devreg_find("usb1", &e) && strlen(e.mount) == DEVREG_MOUNT_MAX - 1);    /* truncated, terminated */
    devreg_set_mount("nothing", "/x");                                              /* unknown name: nothing happens */

    assert(devreg_remove("usb1") == ERR_OK);
    assert(events == 4 && last_event == DEVREG_EVENT_REMOVE && !strcmp(last_name, "usb1"));
    assert(!devreg_find("usb1", &e));
    assert(devreg_remove("usb1") == ERR_NOENT && events == 4);
    assert(devreg_remove(NULL) == ERR_INVAL);

    /* a slot is reusable after removal, and keeps its place in the order only by being added again */
    assert(devreg_add(DEVREG_BLOCK, "usb1", "usb", 1, &usb_dev) == ERR_OK);

    /* foreach: copies, in order of addition, stops when told */
    char names[128] = "";
    assert(devreg_foreach(names_cb, names) == 3 && !strcmp(names, "sd0,camera0,usb1,"));
    int seen = 0;
    assert(devreg_foreach(count_cb, &seen) == 2 && seen == 2);

    /* listener table */
    int hits = 0;
    assert(devreg_subscribe(listener2, &hits) == ERR_OK && hits == 3);
    int dummy[4];
    assert(devreg_subscribe(listener2, &dummy[0]) == ERR_OK);
    assert(devreg_subscribe(listener2, &dummy[1]) == ERR_OK);
    assert(devreg_subscribe(listener2, &dummy[2]) == ERR_BUSY);                       /* four is the limit */
    assert(devreg_subscribe(NULL, NULL) == ERR_INVAL);

    /* the table itself: 16 entries */
    char name[DEVREG_NAME_MAX];
    int added = 0;
    for (int i = 0; i < 20; i++) {
        sprintf(name, "ram%d", i);
        if (devreg_add(DEVREG_BLOCK, name, "ram", (unsigned)i, &sd_dev) == ERR_OK) added++;
    }
    assert(added == 13);                                                                /* 3 were taken already */
    assert(devreg_add(DEVREG_BLOCK, "late", "ram", 0, &sd_dev) == ERR_BUSY);
    assert(devreg_remove("ram0") == ERR_OK);
    assert(devreg_add(DEVREG_BLOCK, "late", "ram", 0, &sd_dev) == ERR_OK);

    puts("PASS device registry: add/find/remove, listeners (replay, removal order), mounts, limits");
    return 0;
}
