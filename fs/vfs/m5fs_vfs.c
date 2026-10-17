/* Adapter: M5FS (fs.h) as a VFS filesystem.  The VFS file handle is the M5FS fd. */
#include <m5os/vfs.h>
#include <m5os/errno.h>
#include <m5os/app.h>

static int m_open(void *fsd, const char *rel, uint32_t flags, void **file)
{
    (void)fsd;
    /* Permissions: reading needs FS_READ; anything that can modify needs FS_WRITE. */
    if ((flags & VFS_O_READ) && !task_has_perm(APP_PERM_FS_READ)) return ERR_PERM;
    if ((flags & (VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC | VFS_O_APPEND)) && !task_has_perm(APP_PERM_FS_WRITE)) return ERR_PERM;
    int fd = fs_open(rel, flags);
    if (fd < 0) return fd;
    *file = (void *)(intptr_t)fd;
    return ERR_OK;
}
static int m_close(void *f)                          { return fs_close((int)(intptr_t)f); }
static int m_read(void *f, void *b, size_t n)        { return fs_read((int)(intptr_t)f, b, n); }
static int m_write(void *f, const void *b, size_t n) { return fs_write((int)(intptr_t)f, b, n); }
static int m_seek(void *f, int32_t o, int w)         { return fs_seek((int)(intptr_t)f, o, w); }
static int m_stat(void *fsd, const char *rel, vfs_info_t *out) { (void)fsd; if (!task_has_perm(APP_PERM_FS_READ)) return ERR_PERM; return fs_stat(rel, out); }
static int m_readdir(void *fsd, const char *rel, int (*cb)(const vfs_info_t *, void *), void *ctx) { (void)fsd; if (!task_has_perm(APP_PERM_FS_READ)) return ERR_PERM; return fs_list(rel, cb, ctx); }
static int m_mkdir(void *fsd, const char *rel)       { (void)fsd; if (!task_has_perm(APP_PERM_FS_WRITE)) return ERR_PERM; return fs_mkdir(rel); }
static int m_unlink(void *fsd, const char *rel)      { (void)fsd; if (!task_has_perm(APP_PERM_FS_WRITE)) return ERR_PERM; return fs_delete(rel); }

const vfs_fs_t vfs_m5fs = {
    .name = "m5fs", .open = m_open, .close = m_close, .read = m_read, .write = m_write,
    .seek = m_seek, .stat = m_stat, .readdir = m_readdir, .mkdir = m_mkdir, .unlink = m_unlink,
};
