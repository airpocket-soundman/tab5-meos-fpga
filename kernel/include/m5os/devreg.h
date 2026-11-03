#pragma once
#include <m5os/types.h>

/*
 * Device registry: the one place where storage and cameras are named, whatever they are plugged into.
 *
 *     sd0  usb0  usb1  usb2  ram0        block devices   (microSD, USB mass storage in slot N, RAM disk)
 *     camera0  camera1                   cameras         (built-in, USB)
 *
 * A driver adds an entry when the device appears and removes it when it goes away; names are chosen by the driver
 * and stay the same for the same slot, so /dev/usb1 is always the second USB storage slot.  Consumers do not
 * know the drivers: the FAT automounter listens for block devices, camsrc and MicroPython open cameras by name,
 * and /dev and `lsdev` list everything.
 *
 * Listeners run in the caller's task, outside the registry's critical section, so they may block (mounting a
 * volume does).  On removal they run before the entry disappears and before the driver frees the device.
 */
#define DEVREG_NAME_MAX  16
#define DEVREG_MOUNT_MAX 24

typedef enum { DEVREG_BLOCK = 1, DEVREG_CAMERA = 2 } devreg_class_t;

/* For DEVREG_CAMERA the object is the camera source: the values of camsrc_id_t. */
#define DEVREG_CAMERA_ONBOARD ((void *)0)
#define DEVREG_CAMERA_USB     ((void *)1)

typedef struct {
    char            name[DEVREG_NAME_MAX];
    devreg_class_t  cls;
    const char     *kind;       /* "sd" "usb" "ram" / "onboard" "usb"; a string that lives forever */
    unsigned        unit;       /* slot number within the kind */
    void           *obj;        /* DEVREG_BLOCK: blockdev_t *; DEVREG_CAMERA: see above */
    char            mount[DEVREG_MOUNT_MAX];   /* where a filesystem on it is mounted, "" if none */
} devreg_entry_t;

typedef enum { DEVREG_EVENT_ADD, DEVREG_EVENT_REMOVE } devreg_event_t;
typedef void (*devreg_listener_t)(devreg_event_t event, const devreg_entry_t *entry, void *ctx);

/* ERR_OK, ERR_INVAL for a bad argument, ERR_BUSY when the name is taken or the table is full. */
int  devreg_add(devreg_class_t cls, const char *name, const char *kind, unsigned unit, void *obj);
/* ERR_NOENT when there is no such device. */
int  devreg_remove(const char *name);
bool devreg_find(const char *name, devreg_entry_t *out);
/* Calls cb with a copy of each entry, in the order they were added; stops when cb returns non-zero.
 * Returns the number of entries seen. */
int  devreg_foreach(int (*cb)(const devreg_entry_t *, void *), void *ctx);
/* Listeners are told about every device already present, then about changes. ERR_BUSY when full. */
int  devreg_subscribe(devreg_listener_t fn, void *ctx);
/* Filesystem drivers record where they mounted a device (shown by lsdev). */
void devreg_set_mount(const char *name, const char *mount);
