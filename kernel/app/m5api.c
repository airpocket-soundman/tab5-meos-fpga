/* The function table handed to every loaded (M5EX) app.  See m5os/m5ex_api.h. */
#include <m5os/m5ex_api.h>
#include <m5os/app.h>
#include <m5os/vfs.h>
#include <m5os/task.h>
#include <m5os/kmalloc.h>
#include <m5os/kprintf.h>
#include <m5os/hal_time.h>
#include <m5os/msg.h>

/* The M5_PERM_* an app requests are the kernel's APP_PERM_* values. */
_Static_assert(M5_PERM_CONSOLE == APP_PERM_CONSOLE && M5_PERM_DISPLAY == APP_PERM_DISPLAY &&
               M5_PERM_INPUT == APP_PERM_INPUT && M5_PERM_FS_READ == APP_PERM_FS_READ &&
               M5_PERM_FS_WRITE == APP_PERM_FS_WRITE && M5_PERM_SPAWN == APP_PERM_SPAWN &&
               M5_PERM_IPC == APP_PERM_IPC, "permission bits must match");
_Static_assert(M5_O_READ == VFS_O_READ && M5_O_WRITE == VFS_O_WRITE && M5_O_CREATE == VFS_O_CREATE &&
               M5_O_TRUNC == VFS_O_TRUNC && M5_O_APPEND == VFS_O_APPEND, "open flags must match");

static uint32_t api_uptime_ms(void) { return (uint32_t)(hal_time_now_us() / 1000u); }
static uint32_t api_task_id(void)   { return task_current()->id; }

const m5_api_t g_m5_api = {
    .version = M5_API_VERSION,
    .size = sizeof(m5_api_t),
    .open = vfs_open, .close = vfs_close, .read = vfs_read, .write = vfs_write,
    .seek = vfs_seek, .ioctl = vfs_ioctl, .unlink = vfs_unlink,
    .sleep_ms = task_sleep, .yield = task_yield, .uptime_ms = api_uptime_ms, .task_id = api_task_id,
    .alloc = umalloc, .release = kfree,
    .snprintf = ksnprintf,
    .msg_send = (int (*)(uint32_t, const m5_msg_t *, uint32_t))msg_send_timeout,
    .msg_receive = (int (*)(m5_msg_t *, uint32_t))msg_receive_timeout,
    .msg_lookup = msg_lookup,
};
_Static_assert(sizeof(msg_t) == sizeof(m5_msg_t) && __builtin_offsetof(msg_t, type) == __builtin_offsetof(m5_msg_t, type) &&
               __builtin_offsetof(msg_t, data) == __builtin_offsetof(m5_msg_t, data), "msg_t and m5_msg_t must have the same layout");
