#include <m5os/vfs.h>
#include <m5os/task.h>
#include <m5os/kprintf.h>

#define TICKER_COUNT  5
#define TICKER_PERIOD_MS 1000u

void ticker_main(void *arg)
{
    (void)arg;
    int out = vfs_open("/dev/uart", VFS_O_WRITE);
    if (out < 0) return;
    for (int i = 1; i <= TICKER_COUNT; i++) {
        task_sleep(TICKER_PERIOD_MS);
        char line[48];
        int n = ksnprintf(line, sizeof line, "ticker (pid %u): %d/%d\n", task_current()->id, i, TICKER_COUNT);
        vfs_write(out, line, (size_t)n);
    }
    vfs_close(out);
}
