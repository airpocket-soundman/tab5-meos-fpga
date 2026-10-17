/* A well-behaved app with file permissions: appends a line to /home/notes.txt, then shows it. */
#include "../apps.h"
#include <m5os/vfs.h>
#include <m5os/hal_time.h>
#include <m5os/kprintf.h>
#include <m5os/task.h>

#define NOTES_PATH   "/home/notes.txt"

void notes_main(void *arg)
{
    (void)arg;
    int out = vfs_open("/dev/uart", VFS_O_WRITE);
    int fd = vfs_open(NOTES_PATH, VFS_O_WRITE | VFS_O_CREATE | VFS_O_APPEND);
    if (fd < 0 || out < 0) return;
    char line[64];
    int n = ksnprintf(line, sizeof line, "note from pid %u at %u ms\n", task_current()->id, (uint32_t)(hal_time_now_us() / 1000u));
    vfs_write(fd, line, (size_t)n);
    vfs_close(fd);

    fd = vfs_open(NOTES_PATH, VFS_O_READ);
    if (fd >= 0) {
        char buf[128];
        int r;
        while ((r = vfs_read(fd, buf, sizeof buf)) > 0) vfs_write(out, buf, (size_t)r);
        vfs_close(fd);
    }
    vfs_close(out);
}
