#include <assert.h>
#include <m5os/errno.h>
#include <m5os/vfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool allocation_fail, mounted = true, close_fail;
static unsigned opens, closes, mutations, reads, writes;
typedef struct {
    bool used, dir;
    char path[96];
    uint8_t data[70000];
    uint32_t size;
} file_t;
static file_t files[8];
static struct {
    bool used;
    file_t *file;
    uint32_t flags, position;
} fds[16];
void *kmalloc_psram(size_t n) { return allocation_fail ? NULL : malloc(n); }
void kfree(void *p) { free(p); }
static void safe(const char *p) { assert(!strncmp(p, "/sd", 3) && (p[3] == 0 || p[3] == '/')); }
static file_t *find(const char *p) {
    for (unsigned i = 0; i < 8; i++)
        if (files[i].used && !strcmp(files[i].path, p))
            return &files[i];
    return NULL;
}
static file_t *make(const char *p, bool dir) {
    for (unsigned i = 0; i < 8; i++)
        if (!files[i].used) {
            memset(&files[i], 0, sizeof files[i]);
            files[i].used = true;
            files[i].dir = dir;
            strcpy(files[i].path, p);
            return &files[i];
        }
    return NULL;
}
int vfs_stat(const char *p, vfs_info_t *out) {
    memset(out, 0, sizeof *out);
    if (!strcmp(p, "/usb") || !strcmp(p, "/usb1") || !strcmp(p, "/usb2") || !strcmp(p, "/usb3"))
        return ERR_NODEV;
    safe(p);
    if (!mounted)
        return ERR_NODEV;
    if (!strcmp(p, "/sd")) {
        out->type = FS_TYPE_DIR;
        return 0;
    }
    file_t *f = find(p);
    if (!f)
        return ERR_NOENT;
    out->type = f->dir ? FS_TYPE_DIR : FS_TYPE_FILE;
    out->size = f->size;
    return 0;
}
int vfs_open(const char *p, uint32_t flags) {
    safe(p);
    if (!mounted)
        return ERR_NODEV;
    opens++;
    file_t *f = find(p);
    if (f && (flags & VFS_O_EXCL))
        return ERR_EXIST;
    if (!f && (flags & VFS_O_CREATE)) {
        f = make(p, false);
        mutations++;
    }
    if (!f)
        return ERR_NOENT;
    if (f->dir)
        return ERR_ISDIR;
    for (unsigned i = 0; i < 16; i++)
        if (!fds[i].used) {
            fds[i].used = true;
            fds[i].file = f;
            fds[i].flags = flags;
            fds[i].position = 0;
            if (flags & VFS_O_TRUNC) {
                f->size = 0;
                mutations++;
            }
            return (int)i;
        }
    return ERR_BUSY;
}
int vfs_close(int fd) {
    assert(fd >= 0 && fd < 16 && fds[fd].used);
    fds[fd].used = false;
    closes++;
    return close_fail ? ERR_IO : 0;
}
int vfs_seek(int fd, int32_t off, int whence) {
    assert(fds[fd].used);
    int64_t p = off + (whence == FS_SEEK_CUR   ? fds[fd].position
                       : whence == FS_SEEK_END ? fds[fd].file->size
                                               : 0);
    if (p < 0 || p > 70000)
        return ERR_INVAL;
    fds[fd].position = (uint32_t)p;
    return (int)p;
}
int vfs_read(int fd, void *data, size_t n) {
    assert(fds[fd].used);
    reads++;
    if (!(fds[fd].flags & VFS_O_READ))
        return ERR_PERM;
    uint32_t left =
        fds[fd].position < fds[fd].file->size ? fds[fd].file->size - fds[fd].position : 0;
    if (n > left)
        n = left;
    memcpy(data, fds[fd].file->data + fds[fd].position, n);
    fds[fd].position += (uint32_t)n;
    return (int)n;
}
int vfs_write(int fd, const void *data, size_t n) {
    assert(fds[fd].used);
    writes++;
    if (!(fds[fd].flags & VFS_O_WRITE))
        return ERR_PERM;
    if (fds[fd].flags & VFS_O_APPEND)
        fds[fd].position = fds[fd].file->size;
    if (n > 7)
        n = 7;
    if (n > 70000 - fds[fd].position)
        return ERR_NOSPC;
    memcpy(fds[fd].file->data + fds[fd].position, data, n);
    fds[fd].position += (uint32_t)n;
    if (fds[fd].position > fds[fd].file->size)
        fds[fd].file->size = fds[fd].position;
    mutations++;
    return (int)n;
}
int vfs_mkdir(const char *p) {
    safe(p);
    if (find(p))
        return ERR_EXIST;
    mutations++;
    return make(p, true) ? 0 : ERR_BUSY;
}
int vfs_unlink(const char *p) {
    safe(p);
    file_t *f = find(p);
    if (!f)
        return ERR_NOENT;
    f->used = false;
    mutations++;
    return 0;
}
int vfs_readdir(const char *p, int (*cb)(const vfs_info_t *, void *), void *arg) {
    safe(p);
    size_t length = strlen(p);
    int count = 0;
    for (unsigned i = 0; i < 8; i++)
        if (files[i].used && !strncmp(files[i].path, p, length) && files[i].path[length] == '/' &&
            !strchr(files[i].path + length + 1, '/')) {
            vfs_info_t entry = {0};
            strcpy(entry.name, files[i].path + length + 1);
            entry.type = files[i].dir ? FS_TYPE_DIR : FS_TYPE_FILE;
            entry.size = files[i].size;
            count++;
            if (cb(&entry, arg))
                break;
        }
    return count;
}
#include "../../user/ssh/sftp_server.c"
static uint8_t request[65540], reply[5000];
static size_t request_size, reply_size;
static void u8(uint8_t x) { request[request_size++] = x; }
static void u32(uint32_t x) {
    u8(x >> 24);
    u8(x >> 16);
    u8(x >> 8);
    u8(x);
}
static void u64(uint64_t x) {
    u32(x >> 32);
    u32((uint32_t)x);
}
static void string(const void *p, size_t n) {
    u32((uint32_t)n);
    memcpy(request + request_size, p, n);
    request_size += n;
}
static void text(const char *p) { string(p, strlen(p)); }
static void req(uint8_t type) {
    request_size = 4;
    u8(type);
    if (type != 1)
        u32(42);
}
static void token(uint32_t h) {
    u32(4);
    u32(h);
}
static void wire(void) {
    size_t n = request_size - 4;
    request[0] = n >> 24;
    request[1] = n >> 16;
    request[2] = n >> 8;
    request[3] = n;
}
static void drain(sftp_server_t *s) {
    reply_size = 0;
    size_t n;
    while ((n = sftp_server_read(s, reply + reply_size, 7)))
        reply_size += n;
    if (reply_size)
        assert(load32(reply) == reply_size - 4);
}
static void send(sftp_server_t *s) {
    wire();
    for (size_t i = 0; i < request_size; i++)
        assert(sftp_server_feed(s, request + i, 1) == 1);
    drain(s);
    assert(!sftp_server_failed(s));
}
static void code(unsigned c) {
    assert(reply_size >= 13 && reply[4] == 101 && load32(reply + 9) == c);
}
static sftp_server_t *session(bool rw) {
    sftp_server_t *s = sftp_server_create(rw);
    assert(s);
    req(1);
    u32(3);
    send(s);
    assert(reply[4] == 2 && load32(reply + 5) == 3);
    return s;
}
static uint32_t open_file(sftp_server_t *s, const char *p, uint32_t flags) {
    req(3);
    text(p);
    u32(flags);
    u32(0);
    send(s);
    if (reply[4] != 102)
        return 0;
    assert(load32(reply + 9) == 4);
    return load32(reply + 13);
}
static void close_file(sftp_server_t *s, uint32_t h) {
    req(4);
    token(h);
    send(s);
}
int main(void) {
    _Static_assert(sizeof(size_t) == 4, "untrusted frame arithmetic must be tested on32-bit host");
    _Static_assert(sizeof(sftp_server_t) < 75000, "session allocation must remain bounded");
    allocation_fail = true;
    assert(!sftp_server_create(false));
    allocation_fail = false;
    file_t *f = make("/sd/file", false);
    f->size = 100;
    for (unsigned i = 0; i < 100; i++)
        f->data[i] = (uint8_t)i;
    sftp_server_t *s = session(false);
    uint32_t h = open_file(s, "/sd/./x/../file", 1);
    assert(h);
    req(5);
    token(h);
    u64(4);
    u32(2);
    send(s);
    assert(reply[4] == 103 && load32(reply + 9) == 2 && reply[13] == 4 && reply[14] == 5);
    unsigned fd = 0;
    assert(fds[fd].position == 6);
    req(8);
    token(h);
    send(s);
    assert(reply[4] == 105 && load32(reply + 17) == 100 && fds[fd].position == 6);
    req(5);
    token(h);
    u64(0x80000000ULL);
    u32(1);
    unsigned read_before = reads;
    send(s);
    code(FX_FAILURE);
    assert(reads == read_before);
    req(6);
    token(h);
    u64(0);
    text("bad");
    send(s);
    code(FX_PERMISSION_DENIED);
    unsigned before = mutations;
    assert(!open_file(s, "/sd/ro-new", 10));
    code(FX_PERMISSION_DENIED);
    assert(before == mutations);
    const char *denied[] = {"/dev/uart", "/sd/../home/a", "/sd/../../sd/file", "../sd/file",
                            "/usb4/a", "/sd/..\\..\\dev", "/sd/0:/file"};
    for (unsigned i = 0; i < sizeof denied / sizeof denied[0]; i++) {
        req(17);
        text(denied[i]);
        send(s);
        code(FX_PERMISSION_DENIED);
    }
    req(17);
    string("/sd/file\0/dev", 13);
    send(s);
    code(FX_PERMISSION_DENIED);
    req(17);
    u32(0xffffffff);
    send(s);
    code(FX_BAD_MESSAGE);
    req(9);
    send(s);
    code(FX_OP_UNSUPPORTED);
    req(18);
    send(s);
    code(FX_OP_UNSUPPORTED);
    close_file(s, h);
    code(0);
    close_file(s, h);
    code(FX_FAILURE);
    req(11);
    text("/");
    send(s);
    h = load32(reply + 13);
    req(12);
    token(h);
    send(s);
    assert(reply[4] == 104 && load32(reply + 13) == 2 && !memcmp(reply + 17, "sd", 2));
    req(12);
    token(h);
    send(s);
    code(FX_EOF);
    req(8);
    token(h);
    send(s);
    assert(reply[4] == 105);
    assert(!sftp_server_destroy(s));
    s = session(true);
    h = open_file(s, "/sd/new", 10);
    assert(h);
    static uint8_t payload[32768];
    for (unsigned i = 0; i < sizeof payload; i++)
        payload[i] = (uint8_t)(i * 7);
    req(6);
    token(h);
    u64(0);
    string(payload, sizeof payload);
    send(s);
    code(0);
    assert(find("/sd/new")->size == sizeof payload &&
           !memcmp(find("/sd/new")->data, payload, sizeof payload));
    close_fail = true;
    close_file(s, h);
    code(FX_FAILURE);
    close_fail = false;
    h = open_file(s, "/sd/exclusive", 42);
    assert(h);
    close_file(s, h);
    code(0);
    assert(!open_file(s, "/sd/exclusive", 42));
    code(FX_FAILURE);
    assert(!open_file(s, "/sd/file", 58));
    code(FX_FAILURE);
    assert(find("/sd/file")->size == 100 && find("/sd/file")->data[99] == 99);
    req(14);
    text("/sd/dir");
    u32(0);
    send(s);
    code(0);
    req(15);
    text("/sd/dir");
    send(s);
    code(0);
    req(13);
    text("/sd/new");
    send(s);
    code(0);
    req(15);
    text("/sd");
    send(s);
    code(FX_PERMISSION_DENIED);
    uint32_t handles[4];
    for (unsigned i = 0; i < 4; i++) {
        handles[i] = open_file(s, "/sd/file", 1);
        assert(handles[i]);
    }
    unsigned opened = opens;
    assert(!open_file(s, "/sd/file", 1) && opens == opened);
    code(FX_FAILURE);
    req(5);
    token(handles[0]);
    u64(0);
    u32(0x100000);
    send(s);
    assert(reply[4] == 103 && load32(reply + 9) == 100);
    mounted = false;
    req(5);
    token(handles[0]);
    u64(0);
    u32(1);
    send(s);
    code(FX_NO_SUCH_FILE);
    mounted = true;
    close_fail = true;
    before = closes;
    assert(sftp_server_destroy(s) == ERR_IO && closes == before + 4);
    close_fail = false;
    /* Coalesced frames stop at a response boundary; caller retains the suffix. */
    s = sftp_server_create(false);
    req(1);
    u32(3);
    wire();
    size_t first = request_size;
    uint8_t both[128];
    memcpy(both, request, first);
    req(17);
    text("/sd/file");
    wire();
    memcpy(both + first, request, request_size);
    assert(sftp_server_feed(s, both, first + request_size) == first);
    assert(!sftp_server_feed(s, both + first, request_size));
    drain(s);
    assert(sftp_server_feed(s, both + first, request_size) == request_size);
    drain(s);
    assert(reply[4] == 105);
    h = open_file(s, "/sd/file", 1);
    assert(h);
    uint8_t huge[] = {0, 1, 0, 1};
    assert(sftp_server_feed(s, huge, 4) == 4 && sftp_server_failed(s));
    before = closes;
    assert(!sftp_server_destroy(s) && closes == before + 1);
    for (unsigned i = 0; i < 2; i++) {
        s = session(false);
        uint32_t bad = i ? 0xffffffffu : 0xfffffffcu;
        uint8_t bad_size[4] = {(uint8_t)(bad >> 24), (uint8_t)(bad >> 16), (uint8_t)(bad >> 8),
                               (uint8_t)bad};
        assert(sftp_server_feed(s, bad_size, 4) == 4 && sftp_server_failed(s));
        assert(!sftp_server_destroy(s));
    }
    s = session(false);
    req(99);
    while (request_size < 4 + SFTP_SERVER_PACKET_MAX)
        u8(0);
    send(s);
    code(FX_OP_UNSUPPORTED);
    assert(!sftp_server_destroy(s));
    puts("PASS SFTP actual engine: framing/path sandbox/RO-RW/32KiB "
         "writes/attrs/FSTAT/handles/cleanup/overflow");
}
