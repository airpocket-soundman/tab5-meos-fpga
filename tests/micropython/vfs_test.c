#define M5OS_MICROPYTHON 1
#include "py/fake.h"
#include <m5os/errno.h>
#include <m5os/vfs.h>
#include <stdio.h>
static struct {
    bool used;
    uint32_t flags;
    int pos;
} fd[16];
static bool registry_alloc_fail;
static unsigned registry_allocs, registry_frees;
void *kmalloc_psram(size_t size) {
    if (registry_alloc_fail)
        return NULL;
    registry_allocs++;
    return malloc(size);
}
void kfree(void *p) {
    if (p)
        registry_frees++;
    free(p);
}
static unsigned opens, closes;
static int close_error, read_error, write_error;
static char last_path[96];
int vfs_normalize(const char *p, char *o) {
    if (!p || p[0] != '/')
        return ERR_INVAL;
    if (strlen(p) >= 96)
        return ERR_NAMETOOLONG;
    strcpy(o, p);
    return 0;
}
int vfs_stat(const char *p, vfs_info_t *i) {
    if (!strcmp(p, "/missing"))
        return ERR_NOENT;
    i->type = !strcmp(p, "/") || !strcmp(p, "/sd") ? FS_TYPE_DIR : FS_TYPE_FILE;
    return 0;
}
int vfs_open(const char *p, uint32_t flags) {
    strcpy(last_path, p);
    opens++;
    if (!strcmp(p, "/missing"))
        return ERR_NOENT;
    for (unsigned i = 0; i < 16; i++)
        if (!fd[i].used) {
            fd[i].used = true;
            fd[i].flags = flags;
            fd[i].pos = 0;
            return i;
        }
    return ERR_BUSY;
}
int vfs_close(int f) {
    assert(fd[f].used);
    fd[f].used = false;
    closes++;
    return close_error;
}
int vfs_read(int f, void *p, size_t n) {
    assert(fd[f].used);
    if (read_error)
        return read_error;
    if (fd[f].pos >= 3)
        return 0;
    if (n > 3 - (unsigned)fd[f].pos)
        n = 3 - fd[f].pos;
    memcpy(p, &"abc"[fd[f].pos], n);
    fd[f].pos += n;
    return n;
}
int vfs_write(int f, const void *p, size_t n) {
    (void)p;
    assert(fd[f].used);
    if (write_error)
        return write_error;
    if (n > 2)
        n = 2;
    fd[f].pos += n;
    return n;
}
int vfs_seek(int f, int32_t o, int w) {
    assert(fd[f].used);
    int64_t n = o + (w == FS_SEEK_CUR ? fd[f].pos : w == FS_SEEK_END ? 3 : 0);
    if (n < 0 || n > INT32_MAX)
        return ERR_INVAL;
    fd[f].pos = n;
    return n;
}
#include "../../user/micropython/meos_vfs.c"
static meos_file_t *open_file(const char *p, const char *m) {
    mp_obj_t a[] = {(void *)p, (void *)m};
    return mp_builtin_open(2, a, NULL);
}
int main(void) {
    meos_vfs_init();
    uint32_t f;
    bool b;
    assert(mode_flags("rb+", 3, &f, &b) && b && f == (VFS_O_READ | VFS_O_WRITE));
    assert(mode_flags("x", 1, &f, &b) && (f & VFS_O_EXCL));
    const char *bad[] = {"", "rr", "rbt", "r++", "wq", "+", "rrrrr"};
    for (unsigned i = 0; i < 7; i++)
        assert(!mode_flags(bad[i], strlen(bad[i]), &f, &b));
    char path[96];
    assert(absolute_path("a\0b", 3, path) == ERR_INVAL);
    assert(!meos_vfs_set_cwd("/sd"));
    meos_file_t *file = open_file("test", "w+b");
    assert(!strcmp(last_path, "/sd/test"));
    assert(binary_stream.is_text == 0 && text_stream.is_text == 1);
    int e = 0;
    assert(file_write(file, "abcd", 4, &e) == 2);
    assert(file_ioctl(file, MP_STREAM_FLUSH, 0, &e) == MP_STREAM_ERROR && e == MP_EOPNOTSUPP);
    struct mp_stream_seek_t seek = {0, FS_SEEK_SET};
    assert(!file_ioctl(file, MP_STREAM_SEEK, (uintptr_t)&seek, &e));
    char data[8];
    assert(file_read(file, data, 3, &e) == 3 && !memcmp(data, "abc", 3));
    seek.offset = 0x80000000LL;
    assert(file_ioctl(file, MP_STREAM_SEEK, (uintptr_t)&seek, &e) == MP_STREAM_ERROR && e > 0);
    close_error = ERR_IO;
    assert(file_ioctl(file, MP_STREAM_CLOSE, 0, &e) == MP_STREAM_ERROR && e == MP_EIO);
    close_error = 0;
    meos_file_t *fresh = open_file("/new", "r");
    assert(file_read(file, data, 1, &e) == MP_STREAM_ERROR && e == MP_EBADF);
    assert(!file_ioctl(file, MP_STREAM_CLOSE, 0, &e));
    assert(file_read(fresh, data, 1, &e) == 1);
    meos_vfs_deinit();
    assert(!file_slot(fresh));
    meos_vfs_init();
    fresh = open_file("/new", "r");
    for (unsigned i = 1; i < 8; i++)
        open_file("/new", "r");
    unsigned before = opens;
    if (!setjmp(failjmp)) {
        open_file("/new", "r");
        assert(0);
    }
    assert(raised == MP_EMFILE && opens == before);
    close_error = ERR_IO;
    before = closes;
    assert(meos_vfs_deinit() == ERR_IO && closes == before + 8);
    close_error = 0;
    registry_alloc_fail = true;
    if (!setjmp(failjmp)) {
        meos_vfs_init();
        assert(0);
    }
    assert(raised == MP_ENOMEM && !slots);
    assert(!meos_vfs_deinit());
    registry_alloc_fail = false;
    meos_vfs_init();
    alloc_fail = true;
    before = opens;
    if (!setjmp(failjmp)) {
        open_file("/new", "r");
        assert(0);
    }
    assert(raised == ENOMEM && opens == before);
    alloc_fail = false;
    assert(mp_import_stat("/missing") == MP_IMPORT_STAT_NO_EXIST &&
           mp_import_stat("/") == MP_IMPORT_STAT_DIR &&
           mp_import_stat("/file") == MP_IMPORT_STAT_FILE);
    mp_reader_t reader;
    mp_reader_new_file(&reader, "/file");
    assert(reader.readbyte(reader.data) == 'a' && reader.readbyte(reader.data) == 'b' &&
           reader.readbyte(reader.data) == 'c' && reader.readbyte(reader.data) == MP_READER_EOF &&
           reader.readbyte(reader.data) == MP_READER_EOF);
    reader.close(reader.data);
    fresh = open_file("/new", "r");
    before = closes;
    reader.close(reader.data);
    assert(closes == before);
    assert(!meos_vfs_deinit());
    meos_vfs_init();
    mp_reader_new_file(&reader, "/file");
    read_error = ERR_NODEV;
    if (!setjmp(failjmp)) {
        reader.readbyte(reader.data);
        assert(0);
    }
    assert(raised == MP_ENODEV);
    read_error = 0;
    assert(!meos_vfs_deinit());
    meos_vfs_init();
    mp_reader_new_file(&reader, "/file");
    close_error = ERR_IO;
    reader.close(reader.data);
    assert(meos_vfs_deinit() == ERR_IO);
    close_error = 0;
    meos_vfs_init();
    lexer_fail = true;
    before = closes;
    if (!setjmp(failjmp)) {
        mp_lexer_new_from_file("/file");
        assert(0);
    }
    assert(raised == ENOMEM && closes == before + 1);
    lexer_fail = false;
    assert(!meos_vfs_deinit());
    assert(registry_allocs == registry_frees);
    puts("PASS production MicroPython VFS "
         "mode/path/stream/seek/error/lifecycle/import-reader/stale-token fault traces");
}
