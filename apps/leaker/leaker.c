/* Allocates and opens things and never gives them back; the kernel must. */
#include "../apps.h"
#include <m5os/vfs.h>
#include <m5os/kmalloc.h>
#include <m5os/task.h>

#define LEAK_SIZES   { 4096u, 8192u, 16384u }
#define LEAK_HANDLES 3

volatile uint32_t app_leaker_ready;

void leaker_main(void *arg)
{
    (void)arg;
    static const uint32_t sizes[] = LEAK_SIZES;
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        uint8_t *p = umalloc(sizes[i]);
        if (p) for (uint32_t k = 0; k < sizes[i]; k += 256) p[k] = 0xA5;
    }
    for (int i = 0; i < LEAK_HANDLES; i++) vfs_open("/dev/uart", VFS_O_WRITE);
    app_leaker_ready = 1;
    for (;;) task_sleep(1000);
}
