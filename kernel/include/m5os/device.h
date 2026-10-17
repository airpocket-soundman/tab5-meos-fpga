#pragma once
#include <m5os/types.h>
#include <m5os/mutex.h>

/*
 * Device abstraction: applications talk to hardware only through
 *
 *     int h = device_open("display");
 *     device_write(h, "hello\n", 6);
 *     device_ioctl(h, DISPLAY_IOC_CLEAR, &color);
 *     device_close(h);
 *
 * and never touch registers or board-specific code.  (Phase 7 will expose the
 * same objects as /dev/<name> through the VFS.)
 *
 * Every operation on a device runs under that device's mutex, so drivers need
 * no locking of their own against other *callers*.  Operations may therefore
 * block briefly but must not wait for another caller of the same device.
 * All operations return >= 0 or a negative ERR_*.
 */
typedef struct device device_t;

typedef struct {
    int (*open)(device_t *dev);                                   /* optional */
    int (*close)(device_t *dev);                                  /* optional */
    int (*read)(device_t *dev, void *buf, size_t len);            /* bytes read */
    int (*write)(device_t *dev, const void *buf, size_t len);     /* bytes written */
    int (*ioctl)(device_t *dev, uint32_t cmd, void *arg);
} device_ops_t;

struct device {
    const char         *name;
    const device_ops_t *ops;
    void               *priv;        /* driver state */
    mutex_t             lock;
    uint32_t            open_count;
    uint32_t            flags;       /* DEV_F_* */
    uint32_t            required_perm;  /* APP_PERM_* an application needs to open it (0 = none) */
};

/* read() does not take the device mutex: for devices whose read may block for a long
 * time (a console waiting for a key) and keep no state that read() shares with write(). */
#define DEV_F_UNLOCKED_READ  0x1u

int  device_register(device_t *dev);             /* ERR_BUSY if table full or name taken */
int  device_open(const char *name);              /* returns a handle >= 0 */
int  device_close(int handle);
int  device_read(int handle, void *buf, size_t len);
int  device_write(int handle, const void *buf, size_t len);
int  device_ioctl(int handle, uint32_t cmd, void *arg);
int  device_foreach(int (*cb)(const device_t *dev, void *ctx), void *ctx);   /* stops if cb != 0; returns count visited */
void device_list(void);                          /* print registered devices */

/* Drivers (one register function each; see kernel/dev/devices.c) */
void devices_init(void);
