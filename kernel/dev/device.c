#include <m5os/device.h>
#include <m5os/errno.h>
#include <m5os/arch.h>
#include <m5os/kprintf.h>
#include <m5os/app.h>

#define DEVICE_MAX   8
#define HANDLE_MAX   16

static device_t *s_devices[DEVICE_MAX];
static struct { device_t *dev; } s_handles[HANDLE_MAX];

static bool name_equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

int device_register(device_t *dev)
{
    if (!dev || !dev->name || !dev->ops) return ERR_INVAL;
    uint32_t irq = arch_irq_disable();
    int slot = -1;
    for (int i = 0; i < DEVICE_MAX; i++) {
        if (s_devices[i] && name_equal(s_devices[i]->name, dev->name)) { slot = -2; break; }
        if (!s_devices[i] && slot == -1) slot = i;
    }
    if (slot >= 0) {
        mutex_init(&dev->lock);
        dev->open_count = 0;           /* flags are set by the driver before registering */
        s_devices[slot] = dev;
    }
    arch_irq_restore(irq);
    return slot >= 0 ? ERR_OK : ERR_BUSY;
}

int device_open(const char *name)
{
    if (!name) return ERR_INVAL;
    device_t *dev = NULL;
    for (int i = 0; i < DEVICE_MAX; i++) {
        if (s_devices[i] && name_equal(s_devices[i]->name, name)) { dev = s_devices[i]; break; }
    }
    if (!dev) return ERR_NODEV;
    if (dev->required_perm && !task_has_perm(dev->required_perm)) return ERR_PERM;

    int handle = ERR_BUSY;
    uint32_t irq = arch_irq_disable();
    for (int i = 0; i < HANDLE_MAX; i++) {
        if (!s_handles[i].dev) { s_handles[i].dev = dev; handle = i; break; }
    }
    arch_irq_restore(irq);
    if (handle < 0) return handle;

    mutex_lock(&dev->lock);
    int rc = dev->ops->open ? dev->ops->open(dev) : ERR_OK;
    if (rc >= 0) dev->open_count++;
    mutex_unlock(&dev->lock);
    if (rc < 0) {
        s_handles[handle].dev = NULL;
        return rc;
    }
    return handle;
}

static device_t *dev_of(int handle)
{
    if (handle < 0 || handle >= HANDLE_MAX) return NULL;
    return s_handles[handle].dev;
}

int device_close(int handle)
{
    device_t *dev = dev_of(handle);
    if (!dev) return ERR_BADF;
    mutex_lock(&dev->lock);
    int rc = dev->ops->close ? dev->ops->close(dev) : ERR_OK;
    dev->open_count--;
    mutex_unlock(&dev->lock);
    s_handles[handle].dev = NULL;
    return rc;
}

int device_read(int handle, void *buf, size_t len)
{
    device_t *dev = dev_of(handle);
    if (!dev) return ERR_BADF;
    if (!dev->ops->read) return ERR_NOSYS;
    if (dev->flags & DEV_F_UNLOCKED_READ) return dev->ops->read(dev, buf, len);
    mutex_lock(&dev->lock);
    int rc = dev->ops->read(dev, buf, len);
    mutex_unlock(&dev->lock);
    return rc;
}

int device_write(int handle, const void *buf, size_t len)
{
    device_t *dev = dev_of(handle);
    if (!dev) return ERR_BADF;
    if (!dev->ops->write) return ERR_NOSYS;
    mutex_lock(&dev->lock);
    int rc = dev->ops->write(dev, buf, len);
    mutex_unlock(&dev->lock);
    return rc;
}

int device_ioctl(int handle, uint32_t cmd, void *arg)
{
    device_t *dev = dev_of(handle);
    if (!dev) return ERR_BADF;
    if (!dev->ops->ioctl) return ERR_NOSYS;
    mutex_lock(&dev->lock);
    int rc = dev->ops->ioctl(dev, cmd, arg);
    mutex_unlock(&dev->lock);
    return rc;
}

int device_foreach(int (*cb)(const device_t *, void *), void *ctx)
{
    int n = 0;
    for (int i = 0; i < DEVICE_MAX; i++) {
        if (!s_devices[i]) continue;
        n++;
        if (cb(s_devices[i], ctx)) break;
    }
    return n;
}

void device_list(void)
{
    kprintf("DEVICE      OPEN  OPS\n");
    for (int i = 0; i < DEVICE_MAX; i++) {
        const device_t *d = s_devices[i];
        if (!d) continue;
        kprintf("%-11s %-4u  %s%s%s%s\n", d->name, d->open_count,
                d->ops->read ? "r" : "-", d->ops->write ? "w" : "-",
                d->ops->ioctl ? "i" : "-", "");
    }
}
