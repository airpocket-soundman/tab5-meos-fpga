#pragma once
/*
 * M5EX application SDK.
 *
 *   #include "m5ex_sdk.h"
 *   M5EX_APP("hello", M5_PERM_CONSOLE, 4096);
 *   void app_main(const m5_api_t *api, void *arg) { ... }
 *
 * Build:  python tools/m5exbuild.py apps/m5ex/hello.c -o hello.m5ex
 */
#include <m5os/m5ex_api.h>

/* The manifest.  The build tool finds the symbol `m5ex_manifest` and copies it
 * into the file header, so an app declares what it needs in its own source. */
typedef struct {
    char     name[16];
    uint32_t permissions;      /* requested M5_PERM_* */
    uint32_t stack_size;
    uint32_t api_version;      /* minimum M5_API_VERSION needed */
} m5ex_manifest_t;

#define M5EX_APP(app_name, perms, stack)                                              \
    const m5ex_manifest_t m5ex_manifest __attribute__((used, section(".rodata.m5ex_manifest"))) = \
        { app_name, (perms), (stack), M5_API_VERSION }

void app_main(const m5_api_t *api, void *arg);

/* tiny C runtime (apps/sdk/m5ex_rt.c): the compiler may emit calls to these */
void  *memcpy(void *d, const void *s, size_t n);
void  *memmove(void *d, const void *s, size_t n);
void  *memset(void *d, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
