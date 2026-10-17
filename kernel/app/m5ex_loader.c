/*
 * M5EX loader: validate a file, copy it into executable RAM, relocate it, start it
 * as an application.  Format and rules: kernel/include/m5os/m5ex.h.
 */
#include <m5os/m5ex.h>
#include <m5os/m5ex_api.h>
#include <m5os/app.h>
#include <m5os/errno.h>
#include <m5os/vfs.h>
#include <m5os/crc32.h>
#include <m5os/kmalloc.h>
#include <m5os/hal_exec.h>
#include <m5os/kprintf.h>
#include "app_internal.h"

extern const m5_api_t g_m5_api;

#define RELOC_SIZE   4u
#define MAX_FILE     (M5EX_HEADER_SIZE + M5EX_MAX_MEM + M5EX_MAX_MEM)   /* image + generous relocation table */

typedef void (*m5ex_entry_t)(const m5_api_t *api, void *arg);

typedef struct { m5ex_entry_t entry; } m5ex_ctx_t;        /* what the app's task is started with */

static void mem_copy(void *d, const void *s, size_t n) { uint8_t *dd = d; const uint8_t *ss = s; while (n--) *dd++ = *ss++; }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* ------------------------------------------------------------- validation */

int m5ex_validate(const uint8_t *file, size_t len, m5ex_header_t *out)
{
    if (!file || len < M5EX_HEADER_SIZE || len > MAX_FILE) return ERR_INVAL;
    m5ex_header_t h;
    mem_copy(&h, file, sizeof h);

    if (h.magic[0] != 'M' || h.magic[1] != '5' || h.magic[2] != 'E' || h.magic[3] != 'X') return ERR_INVAL;
    if (h.version != M5EX_VERSION || h.header_size != M5EX_HEADER_SIZE) return ERR_INVAL;
    if ((h.flags & ~M5EX_ARCH_MASK) != 0 || (h.flags & M5EX_ARCH_MASK) != M5EX_ARCH_THIS) return ERR_INVAL;   /* other CPU's machine code */
    if (h.api_version == 0 || h.api_version > M5_API_VERSION) return ERR_INVAL;      /* needs a newer kernel */
    if (h.permissions & ~APP_PERM_ALL) return ERR_INVAL;

    if (h.stack_size < M5EX_MIN_STACK || h.stack_size > M5EX_MAX_STACK || (h.stack_size & 7u)) return ERR_INVAL;
    if (h.image_size == 0 || h.mem_size < h.image_size || h.mem_size > M5EX_MAX_MEM) return ERR_INVAL;
    if (h.code_size == 0 || h.code_size > h.image_size || (h.code_size & 3u) || (h.image_size & 3u) || (h.mem_size & 3u)) return ERR_INVAL;
    if (h.entry_offset >= h.code_size || (h.entry_offset & 3u)) return ERR_INVAL;
    if (h.reloc_count > h.image_size / RELOC_SIZE) return ERR_INVAL;

    uint64_t expect = (uint64_t)M5EX_HEADER_SIZE + h.image_size + (uint64_t)h.reloc_count * RELOC_SIZE;
    if (expect != len) return ERR_INVAL;                                              /* truncated or trailing bytes */

    bool name_ok = false;
    for (int i = 0; i < (int)sizeof h.name; i++) {
        if (h.name[i] == 0) { name_ok = i > 0; break; }
        if ((uint8_t)h.name[i] < 0x20 || (uint8_t)h.name[i] > 0x7E) return ERR_INVAL;
    }
    if (!name_ok) return ERR_INVAL;

    const uint8_t *image = file + M5EX_HEADER_SIZE;
    const uint8_t *relocs = image + h.image_size;
    if (crc32(image, h.image_size + (size_t)h.reloc_count * RELOC_SIZE) != h.image_crc32) return ERR_CORRUPT;

    /* Every relocation must patch a whole word inside the image, and that word must hold an
     * address inside the module: otherwise relocation could be made to point anywhere. */
    for (uint32_t i = 0; i < h.reloc_count; i++) {
        uint32_t off = rd32(relocs + i * RELOC_SIZE);
        if ((off & 3u) || off > h.image_size - 4u) return ERR_INVAL;
        if (rd32(image + off) >= h.mem_size) return ERR_INVAL;
    }
    if (out) *out = h;
    return ERR_OK;
}

/* --------------------------------------------------------------- loading */

static void m5ex_task_entry(void *arg)
{
    const m5ex_ctx_t *ctx = arg;
    ctx->entry(&g_m5_api, NULL);                        /* returns -> task_exit -> app EXITED */
}

int app_start_image(const uint8_t *file, size_t len, uint32_t *app_id)
{
    m5ex_header_t h;
    int rc = m5ex_validate(file, len, &h);
    if (rc != ERR_OK) return rc;

    app_t *a = app_reserve(h.name, h.permissions);
    if (!a) return ERR_BUSY;

    /* Image and start context are tagged with the app id: they are reclaimed with the app. */
    uint8_t *mem = kmalloc_internal_owned(h.mem_size, a->app_id);
    m5ex_ctx_t *ctx = kmalloc_internal_owned(sizeof *ctx, a->app_id);
    uintptr_t code_base = mem ? hal_exec_alias(mem, h.mem_size) : 0;
    if (!mem || !ctx || !code_base) {
        app_abort(a);
        return mem && ctx ? ERR_NOSYS : ERR_NOMEM;       /* NOSYS: this RAM cannot be executed from */
    }
    uintptr_t data_base = (uintptr_t)mem;

    const uint8_t *image = file + M5EX_HEADER_SIZE;
    const uint8_t *relocs = image + h.image_size;
    mem_copy(mem, image, h.image_size);
    for (uint32_t i = h.image_size; i < h.mem_size; i++) mem[i] = 0;                    /* bss */
    for (uint32_t i = 0; i < h.reloc_count; i++) {
        uint8_t *w = mem + rd32(relocs + i * RELOC_SIZE);
        uint32_t v = rd32(w);                                                           /* link-time address */
        v += (uint32_t)(v < h.code_size ? code_base : data_base);                       /* code -> I bus, data -> D bus */
        w[0] = (uint8_t)v; w[1] = (uint8_t)(v >> 8); w[2] = (uint8_t)(v >> 16); w[3] = (uint8_t)(v >> 24);
    }
    hal_exec_sync(mem, h.mem_size);

    ctx->entry = (m5ex_entry_t)(code_base + h.entry_offset);
    rc = app_launch(a, m5ex_task_entry, ctx, TASK_PRIO_DEFAULT, h.stack_size);
    if (rc == ERR_OK && app_id) *app_id = a->app_id;
    return rc;
}

int app_start_file(const char *path, uint32_t *app_id)
{
    vfs_info_t st;
    int rc = vfs_stat(path, &st);
    if (rc != ERR_OK) return rc;
    if (st.type != FS_TYPE_FILE) return ERR_ISDIR;
    if (st.size < M5EX_HEADER_SIZE || st.size > MAX_FILE) return ERR_INVAL;

    int fd = vfs_open(path, VFS_O_READ);
    if (fd < 0) return fd;
    uint8_t *buf = kmalloc_psram(st.size);
    if (!buf) buf = kmalloc(st.size);
    if (!buf) { vfs_close(fd); return ERR_NOMEM; }

    size_t got = 0;
    int n = 0;
    while (got < st.size && (n = vfs_read(fd, buf + got, st.size - got)) > 0) got += (size_t)n;
    vfs_close(fd);
    rc = (n < 0) ? n : (got == st.size ? app_start_image(buf, got, app_id) : ERR_IO);
    kfree(buf);
    return rc;
}
