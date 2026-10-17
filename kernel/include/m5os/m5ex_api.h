#pragma once
/*
 * The interface between the kernel and a loaded (M5EX) application.
 *
 * Freestanding on purpose (only <stdint.h>/<stddef.h>): the SDK includes this
 * very file, so kernel and apps cannot drift apart.
 *
 * An app never links against kernel symbols.  The loader hands it one pointer,
 * to the table below, as the first argument of app_main():
 *
 *     void app_main(const m5_api_t *api, void *arg);
 *
 * Every function in the table is an ordinary kernel function, so the calling
 * app's permissions are checked exactly as for built-in apps.
 * Growing the table is compatible: new members go at the END and `size` tells
 * an app how much of it exists; `version` changes only for incompatible changes.
 */
#include <stdint.h>
#include <stddef.h>

#define M5_API_VERSION   1u

/* Permission bits an app may request in its manifest (same values as APP_PERM_*). */
#define M5_PERM_CONSOLE  0x01u
#define M5_PERM_DISPLAY  0x02u
#define M5_PERM_INPUT    0x04u
#define M5_PERM_FS_READ  0x08u
#define M5_PERM_FS_WRITE 0x10u
#define M5_PERM_SPAWN    0x20u
#define M5_PERM_IPC      0x40u

/* open() flags (same values as VFS_O_*) */
#define M5_O_READ    0x01u
#define M5_O_WRITE   0x02u
#define M5_O_CREATE  0x04u
#define M5_O_TRUNC   0x08u
#define M5_O_APPEND  0x10u

/* IPC message (same layout as the kernel's msg_t): 32 bytes, fixed. */
typedef struct {
    uint32_t sender;                    /* set by the kernel on delivery; cannot be forged */
    uint32_t type;                      /* meaning is up to the two ends */
    uint8_t  data[24];
} m5_msg_t;

#define M5_WAIT_FOREVER  0xFFFFFFFFu    /* timeout value: block until it can proceed */

typedef struct m5_api {
    uint32_t version;                                   /* M5_API_VERSION */
    uint32_t size;                                      /* sizeof(m5_api_t) as the kernel knows it */

    int      (*open)(const char *path, uint32_t flags); /* >= 0 handle, or negative ERR_* */
    int      (*close)(int h);
    int      (*read)(int h, void *buf, size_t len);
    int      (*write)(int h, const void *buf, size_t len);
    int      (*seek)(int h, int32_t offset, int whence);
    int      (*ioctl)(int h, uint32_t cmd, void *arg);
    int      (*unlink)(const char *path);

    void     (*sleep_ms)(uint32_t ms);
    void     (*yield)(void);
    uint32_t (*uptime_ms)(void);
    uint32_t (*task_id)(void);

    void    *(*alloc)(size_t size);                     /* umalloc: reclaimed when the app ends */
    void     (*release)(void *p);
    int      (*snprintf)(char *buf, size_t cap, const char *fmt, ...);   /* %c %s %d %u %x %p, width, 0, - */

    /* ---- added after the first release: new members only ever go below this line ---- */
    int      (*msg_send)(uint32_t pid, const m5_msg_t *m, uint32_t timeout_ms);   /* 0 = don't wait */
    int      (*msg_receive)(m5_msg_t *m, uint32_t timeout_ms);                    /* 0 = don't wait */
    int      (*msg_lookup)(const char *name);                                     /* pid or negative ERR_* */
} m5_api_t;
