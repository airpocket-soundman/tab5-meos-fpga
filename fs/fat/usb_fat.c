/* FatFs adapter for USB storage and the Tab5 microSD card. All operations serialized by MeOS.
 * No automatic format. On removal, stale handles fail until explicitly closed. */
#include "usb_fat.h"
#include "../../third_party/fatfs/diskio.h"
#include "../../third_party/fatfs/ff.h"
#include <m5os/app.h>
#include <m5os/errno.h>
#include <m5os/kmalloc.h>
#include <m5os/kprintf.h>
#include <m5os/mutex.h>
#include <m5os/vfs.h>
#include <string.h>
typedef struct {
    FATFS fs;
    blockdev_t *bd;
    bool live, mounted;
    unsigned files;
    const char *drive, *mount;
} volume_t;
static volume_t volumes[4] = {{.drive = "0:", .mount = "/usb"},
                              {.drive = "1:", .mount = "/sd"},
                              {.drive = "2:", .mount = "/usb1"},
                              {.drive = "3:", .mount = "/usb2"}};
static unsigned usb_volume(unsigned slot) { return slot ? slot + 1 : 0; }
typedef struct {
    FIL fil;
    volume_t *volume;
} file_t;
static mutex_t s_lock = MUTEX_INIT;
static bool fullpath(volume_t *v, const char *p, char *out) {
    if (strlen(p) + 3 > VFS_PATH_MAX)
        return false;
    ksnprintf(out, VFS_PATH_MAX, "%s%s", v->drive, p);
    return true;
}
static int err(FRESULT r) {
    switch (r) {
    case FR_OK:
        return 0;
    case FR_NO_FILE:
    case FR_NO_PATH:
        return ERR_NOENT;
    case FR_EXIST:
        return ERR_EXIST;
    case FR_WRITE_PROTECTED:
    case FR_DENIED:
        return ERR_PERM;
    case FR_INVALID_NAME:
        return ERR_INVAL;
    case FR_NOT_ENOUGH_CORE:
        return ERR_NOMEM;
    default:
        return ERR_IO;
    }
}
DSTATUS disk_initialize(BYTE p) { return p < 4 && volumes[p].bd ? 0 : STA_NOINIT; }
DSTATUS disk_status(BYTE p) { return p < 4 && volumes[p].bd ? 0 : STA_NOINIT; }
DRESULT disk_read(BYTE p, BYTE *buf, LBA_t lba, UINT n) {
    return p < 4 && volumes[p].bd && volumes[p].bd->read(volumes[p].bd, lba, n, buf) == 0
               ? RES_OK
               : RES_ERROR;
}
DRESULT disk_write(BYTE p, const BYTE *buf, LBA_t lba, UINT n) {
    return p < 4 && volumes[p].bd && volumes[p].bd->write(volumes[p].bd, lba, n, buf) == 0
               ? RES_OK
               : RES_ERROR;
}
DRESULT disk_ioctl(BYTE p, BYTE cmd, void *buf) {
    if (p >= 4 || !volumes[p].bd)
        return RES_NOTRDY;
    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(LBA_t *)buf = volumes[p].bd->block_count;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buf = 512;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buf = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
static int op(void *d, const char *path, uint32_t flags, void **out) {
    volume_t *v = d;
    char resolved[VFS_PATH_MAX];
    if (!fullpath(v, path, resolved))
        return ERR_INVAL;
    if ((flags & VFS_O_READ) && !task_has_perm(APP_PERM_FS_READ))
        return ERR_PERM;
    if ((flags & (VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC | VFS_O_APPEND)) &&
        !task_has_perm(APP_PERM_FS_WRITE))
        return ERR_PERM;
    mutex_lock(&s_lock);
    int rc = ERR_NODEV;
    file_t *handle = NULL;
    FIL *f = NULL;
    if (v->live) {
        handle = kmalloc_psram(sizeof *handle);
        f = handle ? &handle->fil : NULL;
        if (!f)
            rc = ERR_NOMEM;
        else {
            memset(handle, 0, sizeof *handle);
            handle->volume = v;
            BYTE mode = (flags & VFS_O_READ ? FA_READ : 0) | (flags & VFS_O_WRITE ? FA_WRITE : 0);
            if (flags & VFS_O_TRUNC)
                mode |= FA_CREATE_ALWAYS;
            else if (flags & VFS_O_CREATE)
                mode |= FA_OPEN_ALWAYS;
            rc = err(f_open(f, resolved, mode));
            if (!rc && (flags & VFS_O_APPEND))
                rc = err(f_lseek(f, f_size(f)));
            if (!rc) {
                *out = handle;
                v->files++;
            } else {
                f_close(f);
                kfree(handle);
            }
        }
    }
    mutex_unlock(&s_lock);
    return rc;
}
static int cl(void *p) {
    file_t *h = p;
    volume_t *v = h->volume;
    mutex_lock(&s_lock);
    int rc = v->live ? err(f_close(&h->fil)) : ERR_NODEV;
    kfree(p);
    if (v->files)
        v->files--;
    mutex_unlock(&s_lock);
    return rc;
}
static int rd(void *p, void *buf, size_t n) {
    file_t *h = p;
    volume_t *v = h->volume;
    mutex_lock(&s_lock);
    UINT got = 0;
    int rc = v->live ? err(f_read(&h->fil, buf, n, &got)) : ERR_NODEV;
    mutex_unlock(&s_lock);
    return rc ? rc : (int)got;
}
static int wr(void *p, const void *buf, size_t n) {
    file_t *h = p;
    volume_t *v = h->volume;
    mutex_lock(&s_lock);
    UINT got = 0;
    int rc = v->live ? err(f_write(&h->fil, buf, n, &got)) : ERR_NODEV;
    if (!rc)
        rc = err(f_sync(&h->fil));
    mutex_unlock(&s_lock);
    return rc ? rc : (int)got;
}
static int sk(void *p, int32_t off, int whence) {
    file_t *h = p;
    volume_t *v = h->volume;
    mutex_lock(&s_lock);
    FIL *f = &h->fil;
    int rc = ERR_NODEV;
    if (v->live) {
        int64_t base = whence == 0 ? 0 : whence == 1 ? f_tell(f) : whence == 2 ? f_size(f) : -1;
        int64_t pos = base + off;
        rc = base < 0 || pos < 0 || pos > 0x7fffffff ? ERR_INVAL : err(f_lseek(f, pos));
        if (!rc)
            rc = f_tell(f);
    }
    mutex_unlock(&s_lock);
    return rc;
}
static void info(fs_info_t *o, const FILINFO *i) {
    memset(o, 0, sizeof *o);
    const char *name = strlen(i->fname) > FS_NAME_MAX && i->altname[0] ? i->altname : i->fname;
    size_t n = strlen(name);
    if (n > sizeof o->name - 1)
        n = sizeof o->name - 1;
    memcpy(o->name, name, n);
    o->type = (i->fattrib & AM_DIR) ? FS_TYPE_DIR : FS_TYPE_FILE;
    o->size = i->fsize;
}
static int st(void *d, const char *path, vfs_info_t *out) {
    volume_t *v = d;
    char resolved[VFS_PATH_MAX];
    if (!fullpath(v, path, resolved))
        return ERR_INVAL;
    if (!task_has_perm(APP_PERM_FS_READ))
        return ERR_PERM;
    mutex_lock(&s_lock);
    FILINFO i;
    int rc = ERR_NODEV;
    if (v->live) {
        if (!strcmp(path, "/")) {
            memset(out, 0, sizeof *out);
            out->type = FS_TYPE_DIR;
            rc = 0;
        } else {
            rc = err(f_stat(resolved, &i));
            if (!rc)
                info(out, &i);
        }
    }
    mutex_unlock(&s_lock);
    return rc;
}
static int ls(void *d, const char *path, int (*cb)(const vfs_info_t *, void *), void *ctx) {
    volume_t *v = d;
    char resolved[VFS_PATH_MAX];
    if (!fullpath(v, path, resolved))
        return ERR_INVAL;
    if (!task_has_perm(APP_PERM_FS_READ))
        return ERR_PERM;
    mutex_lock(&s_lock);
    FF_DIR dir;
    FILINFO i;
    int rc = v->live ? err(f_opendir(&dir, resolved)) : ERR_NODEV;
    int count = 0;
    if (!rc) {
        for (;;) {
            rc = err(f_readdir(&dir, &i));
            if (rc || !i.fname[0])
                break;
            vfs_info_t o;
            info(&o, &i);
            count++;
            if (cb(&o, ctx))
                break;
        }
        f_closedir(&dir);
    }
    mutex_unlock(&s_lock);
    return rc ? rc : count;
}
static int mk(void *d, const char *p) {
    volume_t *v = d;
    char resolved[VFS_PATH_MAX];
    if (!fullpath(v, p, resolved))
        return ERR_INVAL;
    if (!task_has_perm(APP_PERM_FS_WRITE))
        return ERR_PERM;
    mutex_lock(&s_lock);
    int rc = v->live ? err(f_mkdir(resolved)) : ERR_NODEV;
    mutex_unlock(&s_lock);
    return rc;
}
static int un(void *d, const char *p) {
    volume_t *v = d;
    char resolved[VFS_PATH_MAX];
    if (!fullpath(v, p, resolved))
        return ERR_INVAL;
    if (!task_has_perm(APP_PERM_FS_WRITE))
        return ERR_PERM;
    mutex_lock(&s_lock);
    int rc = v->live ? err(f_unlink(resolved)) : ERR_NODEV;
    mutex_unlock(&s_lock);
    return rc;
}
static const vfs_fs_t ops = {.name = "fat",
                             .open = op,
                             .close = cl,
                             .read = rd,
                             .write = wr,
                             .seek = sk,
                             .stat = st,
                             .readdir = ls,
                             .mkdir = mk,
                             .unlink = un};
static void detach_volume(unsigned index) {
    volume_t *v = &volumes[index];
    mutex_lock(&s_lock);
    v->live = false;
    v->bd = NULL;
    if (!v->files) {
        f_mount(NULL, v->drive, 0);
        if (v->mounted && vfs_umount(v->mount) == 0)
            v->mounted = false;
    }
    mutex_unlock(&s_lock);
}
static bool mount_volume(unsigned index, blockdev_t *bd) {
    volume_t *v = &volumes[index];
    if (!bd)
        return false;
    mutex_lock(&s_lock);
    if (v->files) {
        kprintf("fat: close old file handles before reconnecting\n");
        mutex_unlock(&s_lock);
        return false;
    }
    v->bd = bd;
    FRESULT result = f_mount(&v->fs, v->drive, 1);
    int rc = err(result);
    if (result != FR_OK)
        kprintf("fat: %s FatFs mount result=%u\n", v->mount, (unsigned)result);
    if (result == FR_NO_FILESYSTEM) {
        /* Read-only format hint for unsupported media; never repair/format. */
        uint8_t boot[512];
        if (!bd->read(bd, 0, 1, boot)) {
            if (boot[510] == 0x55 && boot[511] == 0xaa && boot[450] &&
                memcmp(boot + 3, "EXFAT   ", 8) && memcmp(boot + 3, "NTFS    ", 8)) {
                uint32_t lba = (uint32_t)boot[454] | (uint32_t)boot[455] << 8 |
                               (uint32_t)boot[456] << 16 | (uint32_t)boot[457] << 24;
                if (lba && lba < bd->block_count) bd->read(bd, lba, 1, boot);
            }
            const char *hint = !memcmp(boot + 3, "EXFAT   ", 8) ? "exFAT" :
                               !memcmp(boot + 3, "NTFS    ", 8) ? "NTFS" : "unknown";
            kprintf("fat: %s filesystem hint=%s (read-only inspection)\n", v->mount, hint);
        }
    }
    if (!rc) {
        rc = v->mounted ? 0 : vfs_mount(v->mount, &ops, v);
        if (!rc) {
            v->live = v->mounted = true;
            kprintf("fat: mounted at %s\n", v->mount);
        }
    }
    if (rc) {
        v->bd = NULL;
        f_mount(NULL, v->drive, 0);
        kprintf("fat: %s mount failed (%d), no format attempted\n", v->mount, rc);
    }
    mutex_unlock(&s_lock);
    return rc == 0;
}
bool usb_fat_mount_slot(unsigned slot, blockdev_t *bd) {
    return slot < 3 && mount_volume(usb_volume(slot), bd);
}
void usb_fat_detach_slot(unsigned slot) {
    if (slot < 3)
        detach_volume(usb_volume(slot));
}
bool usb_fat_slot_busy(unsigned slot) {
    if (slot >= 3)
        return true;
    mutex_lock(&s_lock);
    bool busy = volumes[usb_volume(slot)].files != 0;
    mutex_unlock(&s_lock);
    return busy;
}
bool usb_fat_mount(blockdev_t *bd) { return usb_fat_mount_slot(0, bd); }
void usb_fat_detach(void) { usb_fat_detach_slot(0); }
bool sd_fat_mount(blockdev_t *bd) { return mount_volume(1, bd); }
