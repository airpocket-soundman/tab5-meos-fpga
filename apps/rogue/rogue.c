/* An app that only holds CONSOLE and tries everything else: every attempt must be refused. */
#include "../apps.h"
#include <m5os/vfs.h>
#include <m5os/errno.h>
#include <m5os/task.h>
#include <m5os/msg.h>
#include <m5os/gui.h>
#include <m5os/kprintf.h>

volatile uint32_t app_rogue_mask;

static void nop_child(void *arg) { (void)arg; }

void rogue_main(void *arg)
{
    (void)arg;
    uint32_t mask = 0;
    int out = vfs_open("/dev/uart", VFS_O_WRITE);
    if (out >= 0) mask |= 8;

    int fd = vfs_open("/dev/display", VFS_O_WRITE);
    if (fd == ERR_PERM) mask |= 1; else if (fd >= 0) vfs_close(fd);

    fd = vfs_open("/home/rogue.txt", VFS_O_WRITE | VFS_O_CREATE);
    if (fd == ERR_PERM) mask |= 2; else if (fd >= 0) vfs_close(fd);

    if (task_create("rogue-child", nop_child, NULL, TASK_PRIO_DEFAULT) == ERR_PERM) mask |= 4;

    msg_t probe = { .type = 1 };
    if (msg_send(1, &probe) == ERR_PERM) mask |= 16;                      /* may not send messages either */

    if (window_create("rogue", 0) == ERR_PERM) mask |= 32;                /* and may not open windows */

    app_rogue_mask = mask;
    if (out >= 0) {
        char line[96];
        int n = ksnprintf(line, sizeof line, "rogue: display %s, file-write %s, spawn %s, ipc %s, window %s\n",
                          (mask & 1) ? "DENIED" : "allowed?!", (mask & 2) ? "DENIED" : "allowed?!", (mask & 4) ? "DENIED" : "allowed?!",
                          (mask & 16) ? "DENIED" : "allowed?!", (mask & 32) ? "DENIED" : "allowed?!");
        vfs_write(out, line, (size_t)n);
        vfs_close(out);
    }
}
