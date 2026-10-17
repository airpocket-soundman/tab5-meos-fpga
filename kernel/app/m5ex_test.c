/* M5EX loader self test: malformed images are refused (and leak nothing); good images run. */
#include <m5os/m5ex.h>
#include <m5os/m5ex_api.h>
#include <m5os/app.h>
#include <m5os/task.h>
#include <m5os/vfs.h>
#include <m5os/errno.h>
#include <m5os/crc32.h>
#include <m5os/kmalloc.h>
#include <m5os/kprintf.h>

/* header field offsets (see m5ex.h) */
enum { O_MAGIC = 0, O_VERSION = 4, O_HDRSIZE = 6, O_FLAGS = 8, O_API = 12, O_PERMS = 16, O_IMAGE = 20, O_MEM = 24,
       O_CODE = 28, O_ENTRY = 32, O_RELOCS = 36, O_STACK = 40, O_CRC = 44, O_NAME = 48 };

#define CK(c) do { if (!(c)) { kprintf("  FAIL line %d: %s\n", __LINE__, #c); ok = false; } } while (0)
#define SETTLE_MS 250u
#define M5TEST_CHECKS 15u        /* CHECK() calls executed by apps/m5ex/m5test.c: keep in sync */

static uint32_t get32(const uint8_t *b, int o) { return b[o] | b[o + 1] << 8 | b[o + 2] << 16 | (uint32_t)b[o + 3] << 24; }
static void put32(uint8_t *b, int o, uint32_t v) { b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8); b[o + 2] = (uint8_t)(v >> 16); b[o + 3] = (uint8_t)(v >> 24); }
static void put16(uint8_t *b, int o, uint16_t v) { b[o] = (uint8_t)v; b[o + 1] = (uint8_t)(v >> 8); }

static void fix_crc(uint8_t *b, size_t len)       /* recompute the checksum so a test isolates ONE other check */
{
    put32(b, O_CRC, crc32(b + M5EX_HEADER_SIZE, len - M5EX_HEADER_SIZE));
}

static const m5ex_image_t *find_image(const char *name)
{
    int n;
    const m5ex_image_t *img = m5ex_embedded(&n);
    for (int i = 0; i < n; i++) {
        const char *a = img[i].name, *b = name;
        while (*a && *a == *b) { a++; b++; }
        if (*a == *b) return &img[i];
    }
    return NULL;
}

static bool app_info_of(uint32_t id, app_info_t *out)
{
    app_info_t all[APP_MAX];
    int n = app_list(all, APP_MAX);
    for (int i = 0; i < n; i++) if (all[i].app_id == id) { *out = all[i]; return true; }
    return false;
}

static int s_rejected;

/* Every mutation must be refused, and the loader must not allocate for a bad file. */
static bool refuses(const char *what, const uint8_t *buf, size_t len, int want_rc)
{
    kmem_stats_t b, a;
    kmem_internal_stats(&b);
    int rc = m5ex_validate(buf, len, NULL);
    uint32_t id = 0;
    int rc2 = app_start_image(buf, len, &id);
    kmem_internal_stats(&a);
    bool good = rc < 0 && rc2 < 0 && (want_rc == 0 || rc == want_rc) && a.used == b.used;
    if (!good) kprintf("  FAIL (%s): validate=%d start=%d leaked=%d\n", what, rc, rc2, (int)(a.used - b.used));
    else s_rejected++;
    return good;
}

bool m5ex_selftest(void)
{
    bool ok = true;
    kmem_stats_t base, now;
    kmem_internal_stats(&base);
    int tasks0; { task_info_t t[TASK_MAX]; tasks0 = task_snapshot(t, TASK_MAX); }

    const m5ex_image_t *hello = find_image("m5hello");
    const m5ex_image_t *test = find_image("m5test");
    CK(hello && test && find_image("m5spin"));
    if (!hello || !test) return false;

    /* ---- validation ------------------------------------------------------- */
    m5ex_header_t h;
    CK(m5ex_validate(hello->data, hello->size, &h) == ERR_OK);
    CK(h.code_size > 0 && h.reloc_count == 2 && h.permissions == M5_PERM_CONSOLE);
    CK(m5ex_validate(test->data, test->size, &h) == ERR_OK && h.reloc_count >= 10);
    CK(m5ex_validate(NULL, 0, NULL) < 0);

    size_t len = hello->size;
    uint8_t *b = kmalloc(len + 8);
    if (!b) return false;
#define FRESH()  do { for (size_t i = 0; i < len + 8; i++) b[i] = i < len ? hello->data[i] : 0; } while (0)
    uint32_t image = get32(hello->data, O_IMAGE);
    uint32_t code = get32(hello->data, O_CODE);
    size_t reloc_tab = M5EX_HEADER_SIZE + image;

    s_rejected = 0;
    FRESH(); b[O_MAGIC] ^= 1;                                   CK(refuses("magic", b, len, 0));
    FRESH(); put16(b, O_VERSION, 2);                            CK(refuses("version", b, len, 0));
    FRESH(); put16(b, O_HDRSIZE, 65);                           CK(refuses("header_size", b, len, 0));
    FRESH(); put32(b, O_FLAGS, 1);                              CK(refuses("flags", b, len, 0));
    FRESH(); put32(b, O_API, M5_API_VERSION + 1);               CK(refuses("api too new", b, len, 0));
    FRESH(); put32(b, O_API, 0);                                CK(refuses("api zero", b, len, 0));
    FRESH(); put32(b, O_PERMS, 0x80);                           CK(refuses("unknown permission", b, len, 0));
    FRESH(); put32(b, O_IMAGE, image + 4);                      CK(refuses("image_size too big", b, len, 0));
    FRESH();                                                    CK(refuses("truncated", b, len - 1, 0));
    FRESH();                                                    CK(refuses("trailing byte", b, len + 1, 0));
    FRESH();                                                    CK(refuses("shorter than a header", b, 32, 0));
    FRESH(); put32(b, O_MEM, image - 4);                        CK(refuses("mem < image", b, len, 0));
    FRESH(); put32(b, O_MEM, 512u * 1024u);                     CK(refuses("mem too large", b, len, 0));
    FRESH(); put32(b, O_CODE, image + 4);                       CK(refuses("code > image", b, len, 0));
    FRESH(); put32(b, O_CODE, 2);                               CK(refuses("code unaligned", b, len, 0));
    FRESH(); put32(b, O_ENTRY, code);                           CK(refuses("entry outside code", b, len, 0));
    FRESH(); put32(b, O_ENTRY, 2);                              CK(refuses("entry unaligned", b, len, 0));
    FRESH(); put32(b, O_RELOCS, 0x40000000u);                   CK(refuses("reloc_count absurd", b, len, 0));
    FRESH(); put32(b, O_STACK, 1024);                           CK(refuses("stack too small", b, len, 0));
    FRESH(); put32(b, O_STACK, 65536);                          CK(refuses("stack too large", b, len, 0));
    FRESH(); put32(b, O_STACK, 4097);                           CK(refuses("stack unaligned", b, len, 0));
    FRESH(); for (int i = 0; i < 16; i++) b[O_NAME + i] = 'A';  CK(refuses("name not terminated", b, len, 0));
    FRESH(); b[O_NAME] = 0;                                     CK(refuses("name empty", b, len, 0));
    FRESH(); b[O_NAME + 1] = 1;                                 CK(refuses("name unprintable", b, len, 0));
    FRESH(); b[M5EX_HEADER_SIZE + 10] ^= 0xFF;                  CK(refuses("payload bit flip", b, len, ERR_CORRUPT));
    FRESH(); put32(b, (int)reloc_tab, 2);        fix_crc(b, len); CK(refuses("reloc unaligned", b, len, 0));
    FRESH(); put32(b, (int)reloc_tab, image);    fix_crc(b, len); CK(refuses("reloc out of image", b, len, 0));
    FRESH(); put32(b, M5EX_HEADER_SIZE, 0x7FFF0000u); fix_crc(b, len); CK(refuses("reloc target outside module", b, len, 0));
    FRESH();                                                    CK(m5ex_validate(b, len, NULL) == ERR_OK);   /* the pristine copy still passes */
    CK(s_rejected == 28);
    kfree(b);

    /* ---- running ----------------------------------------------------------- */
    uint32_t id;
    app_info_t info;
    CK(app_start_image(hello->data, hello->size, &id) == ERR_OK);
    task_sleep(SETTLE_MS);
    CK(app_info_of(id, &info) && info.state == APP_EXITED && info.permissions == M5_PERM_CONSOLE);
    CK(info.name[0] == 'm' && info.name[1] == '5' && info.name[2] == 'h');                   /* name came from the header */

    vfs_unlink("/home/m5test.txt");
    CK(app_start_file("/apps/m5test.m5ex", &id) == ERR_OK);                                  /* from the filesystem */
    task_sleep(SETTLE_MS);
    CK(app_info_of(id, &info) && info.state == APP_EXITED);
    char res[40] = { 0 };
    int fd = vfs_open("/home/m5test.txt", VFS_O_READ);
    int n = fd >= 0 ? vfs_read(fd, res, sizeof res - 1) : -1;
    if (fd >= 0) vfs_close(fd);
    const char *want = "m5test PASS ";
    bool prefix = n > 12;
    for (int i = 0; prefix && i < 12; i++) if (res[i] != want[i]) prefix = false;
    /* "N/N": the app counted its own checks; PASS means every one agreed */
    uint32_t p1 = 0, p2 = 0; int i = 12;
    while (res[i] >= '0' && res[i] <= '9') p1 = p1 * 10 + (uint32_t)(res[i++] - '0');
    if (res[i] == '/') i++;
    while (res[i] >= '0' && res[i] <= '9') p2 = p2 * 10 + (uint32_t)(res[i++] - '0');
    CK(prefix && p1 == p2 && p1 == M5TEST_CHECKS);
    if (!(prefix && p1 == p2)) kprintf("  m5test said: %s", res);
    vfs_unlink("/home/m5test.txt");

    CK(app_start_file("/apps/does-not-exist.m5ex", &id) == ERR_NOENT);
    CK(app_start_file("/apps", &id) == ERR_ISDIR);
    CK(app_start_file("/system/version", &id) == ERR_INVAL);                                 /* a real file, not an M5EX */

    /* a loaded app's memory (code image + its own allocations) comes back when it is stopped */
    CK(app_start_file("/apps/m5spin.m5ex", &id) == ERR_OK);
    task_sleep(SETTLE_MS);
    CK(app_info_of(id, &info) && info.state == APP_RUNNING && info.mem_bytes >= 10000u + 72u);
    CK(app_stop(id) == ERR_OK);
    task_sleep(SETTLE_MS);
    CK(app_info_of(id, &info) && info.state == APP_KILLED && info.mem_bytes == 0 && info.reclaimed_bytes >= 10000u + 72u);

    task_sleep(SETTLE_MS);
    kmem_internal_stats(&now);
    CK(now.used == base.used);                                                              /* no leak anywhere */
    int tasks1; { task_info_t t[TASK_MAX]; tasks1 = task_snapshot(t, TASK_MAX); }
    CK(tasks1 == tasks0);
    kprintf("m5ex selftest: %s (%d malformed images refused)\n", ok ? "PASS" : "FAIL", s_rejected);
    return ok;
}
