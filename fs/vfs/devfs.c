/* devfs: every device registered in Phase 5 appears as /dev/<name>. */
#include <m5os/vfs.h>
#include <m5os/device.h>
#include <m5os/errno.h>

static bool is_root(const char *rel) { return rel[0] == '/' && rel[1] == 0; }

static int devfs_open(void *fsd, const char *rel, uint32_t flags, void **file)
{
    (void)fsd;
    if (flags & VFS_O_EXCL) return ERR_NOSYS;
    if (is_root(rel)) return ERR_ISDIR;
    int h = device_open(rel + 1);                 /* skip the leading '/' */
    if (h < 0) return h == ERR_NODEV ? ERR_NOENT : h;
    *file = (void *)(intptr_t)h;
    return ERR_OK;
}

static int devfs_close(void *file) { return device_close((int)(intptr_t)file); }
static int devfs_read(void *file, void *buf, size_t len) { return device_read((int)(intptr_t)file, buf, len); }
static int devfs_write(void *file, const void *buf, size_t len) { return device_write((int)(intptr_t)file, buf, len); }
static int devfs_ioctl(void *file, uint32_t cmd, void *arg) { return device_ioctl((int)(intptr_t)file, cmd, arg); }

static int fill_cb(const device_t *dev, void *ctx)
{
    vfs_info_t *want = ctx;
    const char *a = dev->name, *b = want->name;
    while (*a && *a == *b) { a++; b++; }
    if (*a == *b) want->type = FS_TYPE_DEV;       /* mark found */
    return 0;
}

static int devfs_stat(void *fsd, const char *rel, vfs_info_t *out)
{
    (void)fsd;
    if (is_root(rel)) { out->name[0] = 'd'; out->name[1] = 'e'; out->name[2] = 'v'; out->name[3] = 0; out->type = FS_TYPE_DIR; out->size = 0; return ERR_OK; }
    vfs_info_t probe = { .type = 0 };
    int k = 0;
    while (rel[1 + k] && k < FS_NAME_MAX) { probe.name[k] = rel[1 + k]; k++; }
    probe.name[k] = 0;
    device_foreach(fill_cb, &probe);
    if (probe.type != FS_TYPE_DEV) return ERR_NOENT;
    probe.size = 0;
    *out = probe;
    return ERR_OK;
}

struct list_ctx { int (*cb)(const vfs_info_t *, void *); void *ctx; };

static int list_cb(const device_t *dev, void *c)
{
    struct list_ctx *lc = c;
    vfs_info_t info = { .type = FS_TYPE_DEV, .size = 0 };
    int k = 0;
    while (dev->name[k] && k < FS_NAME_MAX) { info.name[k] = dev->name[k]; k++; }
    info.name[k] = 0;
    return lc->cb(&info, lc->ctx);
}

static int devfs_readdir(void *fsd, const char *rel, int (*cb)(const vfs_info_t *, void *), void *ctx)
{
    (void)fsd;
    if (!is_root(rel)) return ERR_NOTDIR;
    struct list_ctx lc = { cb, ctx };
    return device_foreach(list_cb, &lc);
}

const vfs_fs_t vfs_devfs = {
    .name = "devfs", .open = devfs_open, .close = devfs_close, .read = devfs_read,
    .write = devfs_write, .ioctl = devfs_ioctl, .stat = devfs_stat, .readdir = devfs_readdir,
};
