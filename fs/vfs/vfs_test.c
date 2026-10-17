/* VFS self test: uses ONLY the vfs_* API, on the real root mount and /dev. */
#include <m5os/vfs.h>
#include <m5os/errno.h>
#include <m5os/kprintf.h>
#include <m5os/dev_display.h>
#include <m5os/dev_buttons.h>

#define CHECK(cond) do { if (!(cond)) { kprintf("  FAIL line %d: %s\n", __LINE__, #cond); ok = false; goto done; } } while (0)

static bool str_eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

struct seen { unsigned mask; int n; };

static int collect_cb(const vfs_info_t *i, void *ctx)
{
    struct seen *s = ctx;
    s->n++;
    static const char *const names[] = { "uart", "display", "buttons", "dev", "home", "apps", "system" };
    for (unsigned k = 0; k < sizeof names / sizeof names[0]; k++) if (str_eq(i->name, names[k])) s->mask |= 1u << k;
    return 0;
}
static int stop_cb(const vfs_info_t *i, void *ctx) { (void)i; (*(int *)ctx)++; return 1; }

bool vfs_selftest(void)
{
    bool ok = true;
    int fd = -1;
    vfs_info_t info;

    /* --- namespace: files and devices side by side ---------------------- */
    CHECK(vfs_stat("/", &info) == ERR_OK && info.type == FS_TYPE_DIR);
    CHECK(vfs_stat("/dev", &info) == ERR_OK && info.type == FS_TYPE_DIR);
    CHECK(vfs_stat("/dev/uart", &info) == ERR_OK && info.type == FS_TYPE_DEV);
    CHECK(vfs_stat("/dev/nothing", &info) == ERR_NOENT);
    CHECK(vfs_stat("/home", &info) == ERR_OK && info.type == FS_TYPE_DIR);

    struct seen s = { 0, 0 };
    CHECK(vfs_readdir("/dev", collect_cb, &s) >= 3);
    CHECK((s.mask & 0x7u) == 0x7u);                                   /* uart display buttons */
    s = (struct seen){ 0, 0 };
    CHECK(vfs_readdir("/", collect_cb, &s) >= 4);
    CHECK((s.mask & 0x78u) == 0x78u);                                 /* dev(mount point) home apps system */
    int stops = 0;
    CHECK(vfs_readdir("/", stop_cb, &stops) >= 1 && stops == 1);      /* callback can stop the walk */

    /* --- devices through the same calls as files ------------------------ */
    fd = vfs_open("/dev/uart", VFS_O_WRITE);
    CHECK(fd >= 0);
    static const char hello[] = "vfs: written through /dev/uart\n";
    CHECK(vfs_write(fd, hello, sizeof hello - 1) == (int)sizeof hello - 1);
    CHECK(vfs_seek(fd, 0, FS_SEEK_SET) == ERR_NOSYS);                 /* a device has no seek */
    CHECK(vfs_close(fd) == ERR_OK);

    fd = vfs_open("/dev/buttons", VFS_O_READ);
    CHECK(fd >= 0);
    uint8_t state = 0xFF;
    CHECK(vfs_read(fd, &state, 1) == 1 && (state & ~0x07u) == 0);     /* only bits A/B/C */
    CHECK(vfs_close(fd) == ERR_OK);

    fd = vfs_open("/dev/display", VFS_O_READ | VFS_O_WRITE);
    CHECK(fd >= 0);
    display_info_t di;
    CHECK(vfs_ioctl(fd, DISPLAY_IOC_GET_INFO, &di) == ERR_OK && di.width == 320 && di.height == 240);
    CHECK(vfs_close(fd) == ERR_OK);

    /* --- regular files --------------------------------------------------- */
    fd = vfs_open("/home/vfs.txt", VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
    CHECK(fd >= 0 && vfs_write(fd, "0123456789", 10) == 10 && vfs_close(fd) == ERR_OK);
    fd = vfs_open("/home/vfs.txt", VFS_O_READ);
    CHECK(fd >= 0 && vfs_seek(fd, 4, FS_SEEK_SET) == 4);
    char b[8];
    CHECK(vfs_read(fd, b, 3) == 3 && b[0] == '4' && b[2] == '6');
    CHECK(vfs_close(fd) == ERR_OK);
    CHECK(vfs_stat("/home/vfs.txt", &info) == ERR_OK && info.type == FS_TYPE_FILE && info.size == 10);

    /* --- path normalization and mount-point boundaries ------------------- */
    fd = vfs_open("/home/../home//./vfs.txt", VFS_O_READ);
    CHECK(fd >= 0 && vfs_close(fd) == ERR_OK);
    fd = vfs_open("/dev/./uart", VFS_O_WRITE);
    CHECK(fd >= 0 && vfs_close(fd) == ERR_OK);
    fd = vfs_open("/dev/../home/vfs.txt", VFS_O_READ);
    CHECK(fd >= 0 && vfs_close(fd) == ERR_OK);
    CHECK(vfs_open("/devices", VFS_O_READ) == ERR_NOENT);             /* "/dev" must not match "/devices" */
    CHECK(vfs_open("/dev", VFS_O_READ) == ERR_ISDIR);
    CHECK(vfs_open("/home", VFS_O_READ) == ERR_ISDIR);
    CHECK(vfs_open("relative/path", VFS_O_READ) == ERR_INVAL);
    CHECK(vfs_open("/dev/none", VFS_O_READ) == ERR_NOENT);

    /* --- handles ---------------------------------------------------------- */
    CHECK(vfs_read(fd, b, 1) == ERR_BADF);                            /* fd was closed above */
    CHECK(vfs_close(fd) == ERR_BADF);
    CHECK(vfs_read(-1, b, 1) == ERR_BADF && vfs_write(VFS_FD_MAX, b, 1) == ERR_BADF);
    /* Exhaust handles.  Files: the M5FS table (8) fills before the VFS table (16), and the
     * failed open must give its reserved VFS slot back. Devices: then the VFS table itself fills. */
    int fds[VFS_FD_MAX];
    int n = 0;
    while (n < VFS_FD_MAX && (fds[n] = vfs_open("/home/vfs.txt", VFS_O_READ)) >= 0) n++;
    CHECK(n >= 8 && n < VFS_FD_MAX);                                  /* stopped by the filesystem, with an error code */
    CHECK(vfs_open("/home/vfs.txt", VFS_O_READ) < 0);
    while (n--) vfs_close(fds[n]);
    /* Other tasks (ui, logA, logB) hold VFS handles of their own, so the free count is not
     * ours to know.  Check behaviours that do not depend on it:
     *  - failed opens never leak a slot (more failures than the table has slots);
     *  - exhaustion is reported as an error, not a crash, and closing recovers. */
    for (int i = 0; i < 3 * VFS_FD_MAX; i++) CHECK(vfs_open("/home/missing.txt", VFS_O_READ) == ERR_NOENT);
    for (int i = 0; i < 3 * VFS_FD_MAX; i++) CHECK(vfs_open("/dev/none", VFS_O_READ) == ERR_NOENT);
    n = 0;
    while (n < VFS_FD_MAX && (fds[n] = vfs_open("/dev/uart", VFS_O_WRITE)) >= 0) n++;
    CHECK(n >= 1 && n <= VFS_FD_MAX);
    if (n < VFS_FD_MAX) CHECK(fds[n] < 0);                            /* table (or device handles) full: an error code */
    while (n--) vfs_close(fds[n]);
    fd = vfs_open("/dev/uart", VFS_O_WRITE);                          /* everything usable again */
    CHECK(fd >= 0 && vfs_close(fd) == ERR_OK);
    fd = -1;

    int busy_fd = vfs_open("/dev/uart", VFS_O_WRITE);
    CHECK(busy_fd >= 0);
    CHECK(vfs_umount("/dev") == ERR_BUSY);                            /* refuses while a handle is open */
    CHECK(vfs_close(busy_fd) == ERR_OK);
    CHECK(vfs_umount("/nonexistent") == ERR_NOENT);

    /* --- mutation ---------------------------------------------------------- */
    CHECK(vfs_mkdir("/home/sub") == ERR_OK);
    CHECK(vfs_stat("/home/sub", &info) == ERR_OK && info.type == FS_TYPE_DIR);
    CHECK(vfs_unlink("/home/sub") == ERR_OK);
    CHECK(vfs_unlink("/home/vfs.txt") == ERR_OK);
    CHECK(vfs_unlink("/home/vfs.txt") == ERR_NOENT);
    CHECK(vfs_unlink("/dev/uart") == ERR_NOSYS);                      /* devfs is not removable */
    CHECK(vfs_mkdir("/dev/newdev") == ERR_NOSYS);

done:
    if (fd >= 0) vfs_close(fd);
    kprintf("vfs selftest: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}
