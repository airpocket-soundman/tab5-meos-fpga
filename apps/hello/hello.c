/* An application uses only the OS API: here the VFS and the task API. */
#include <m5os/vfs.h>
#include <m5os/task.h>
#include <m5os/kprintf.h>

void hello_main(void *arg)
{
    (void)arg;
    int out = vfs_open("/dev/uart", VFS_O_WRITE);
    if (out < 0) return;
    char line[64];
    int n = ksnprintf(line, sizeof line, "Hello from the hello app (pid %u)!\n", task_current()->id);
    vfs_write(out, line, (size_t)n);
    vfs_close(out);
}
