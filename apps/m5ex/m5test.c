/*
 * Self-checking M5EX app.  Every construct below depends on the loader doing its
 * job: pointers in .data/.rodata (relocations to code and to data), the
 * zero-initialised .bss, the runtime library, libgcc, calls through the kernel's
 * API table.  The result goes to /home/m5test.txt for the kernel test to read.
 */
#include "../sdk/m5ex_sdk.h"

M5EX_APP("m5test", M5_PERM_CONSOLE | M5_PERM_FS_READ | M5_PERM_FS_WRITE, 4096);

typedef int (*binop_t)(int, int);
static int op_add(int a, int b) { return a + b; }
static int op_sub(int a, int b) { return a - b; }
static int op_mul(int a, int b) { return a * b; }

static const binop_t   ops[]   = { op_add, op_sub, op_mul };        /* .rodata words -> CODE relocations */
static const char *const names[] = { "add", "sub", "mul" };         /* .rodata words -> DATA relocations */
static int   table[8]  = { 1, 2, 3, 4, 5, 6, 7, 8 };                /* initialised .data */
static int  *table_ptr = &table[3];                                 /* .data word -> .data (relocated) */
static int   zeros[32];                                             /* .bss */
static int   counter;
static struct { int a[10]; } s1 = { { 10, 11, 12, 13, 14, 15, 16, 17, 18, 19 } }, s2;

static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

static int sw(int x)
{
    switch (x) {                                                    /* built without jump tables */
    case 0: return 10; case 1: return 21; case 2: return 32; case 3: return 43; case 5: return 54;
    default: return -1;
    }
}

static int checks, passed;
#define CHECK(c) do { checks++; if (c) passed++; } while (0)

void app_main(const m5_api_t *api, void *arg)
{
    (void)arg;
    volatile int k = 7;                                             /* keep the compiler from folding everything */
    volatile uint32_t u = 1000003u;

    CHECK(ops[0](k, 5) == 12 && ops[1](k, 5) == 2 && ops[2](k, 5) == 35);          /* indirect calls into the image */
    CHECK(names[0][0] == 'a' && names[1][0] == 's' && names[2][2] == 'l' && strlen(names[2]) == 3);
    CHECK(*table_ptr == 4 && table[7] == 8 && table_ptr == &table[3]);
    int z = 0; for (int i = 0; i < 32; i++) z |= zeros[i];
    CHECK(z == 0);                                                  /* bss really is zero */
    counter += k; counter += 3;
    CHECK(counter == 10);
    CHECK(fib(15) == 610);                                          /* recursion: stack and windowed calls */
    CHECK(sw(0) == 10 && sw(3) == 43 && sw(5) == 54 && sw(4) == -1 && sw(k) == -1);
    CHECK((u / 7u) == 142857u && (u % 7u) == 4u);                   /* hardware divide */
    uint64_t big = (uint64_t)u * (uint64_t)u;                       /* 64-bit multiply: libgcc */
    CHECK(big == 1000006000009ull);
    CHECK(big / 1000003u == 1000003u);
    s2 = s1;                                                        /* struct copy -> memcpy from the SDK runtime */
    CHECK(memcmp(&s1, &s2, sizeof s1) == 0 && s2.a[9] == 19);
    memset(zeros, 0x5A, sizeof zeros);
    CHECK(((uint8_t *)zeros)[127] == 0x5A);

    uint8_t *p = api->alloc(256);                                   /* kernel API table: memory tagged to this app */
    CHECK(p != 0);
    if (p) { memset(p, 0xC3, 256); CHECK(p[255] == 0xC3); api->release(p); }
    CHECK(api->uptime_ms() > 0 && api->task_id() > 0 && api->version == M5_API_VERSION);

    char line[48];
    int n = api->snprintf(line, sizeof line, "m5test %s %d/%d\n", passed == checks ? "PASS" : "FAIL", passed, checks);
    int fd = api->open("/home/m5test.txt", M5_O_WRITE | M5_O_CREATE | M5_O_TRUNC);
    if (fd >= 0) { api->write(fd, line, (size_t)n); api->close(fd); }
    int out = api->open("/dev/uart", M5_O_WRITE);
    if (out >= 0) { api->write(out, line, (size_t)n); api->close(out); }
}
