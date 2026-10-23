/* SFTP v3 bounded server engine. No shell execution or device namespace access. */
#include "sftp_server.h"
#include <limits.h>
#include <m5os/errno.h>
#include <m5os/kmalloc.h>
#include <m5os/vfs.h>
#include <string.h>
#define FX_OK 0u
#define FX_EOF 1u
#define FX_NO_SUCH_FILE 2u
#define FX_PERMISSION_DENIED 3u
#define FX_FAILURE 4u
#define FX_BAD_MESSAGE 5u
#define FX_OP_UNSUPPORTED 8u
#define ATTR_SIZE 1u
#define ATTR_UIDGID 2u
#define ATTR_PERMISSIONS 4u
#define ATTR_ACMODTIME 8u
#define ATTR_EXTENDED 0x80000000u
#define P_READ 1u
#define P_WRITE 2u
#define P_APPEND 4u
#define P_CREATE 8u
#define P_TRUNC 16u
#define P_EXCL 32u
#define OUTPUT_MAX (SFTP_SERVER_READ_MAX + 512u)
typedef struct {
    const uint8_t *data;
    size_t length, at;
    bool ok;
} reader_t;
typedef struct {
    bool used, directory, root;
    uint32_t token, flags, cursor;
    int fd;
    char path[VFS_PATH_MAX];
} handle_t;
struct sftp_server {
    bool writable, initialized, failed;
    uint32_t serial;
    size_t input_used, input_need, output_size, output_at;
    uint8_t input[SFTP_SERVER_PACKET_MAX + 4u], output[OUTPUT_MAX];
    handle_t handles[SFTP_SERVER_HANDLES_MAX];
};
static uint32_t load32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint32_t get32(reader_t *r) {
    if (!r->ok || r->at > r->length || r->length - r->at < 4) {
        r->ok = false;
        return 0;
    }
    uint32_t v = load32(r->data + r->at);
    r->at += 4;
    return v;
}
static uint64_t get64(reader_t *r) {
    uint64_t hi = get32(r), lo = get32(r);
    return hi << 32 | lo;
}
static const uint8_t *getstr(reader_t *r, uint32_t *length) {
    uint32_t n = get32(r);
    if (!r->ok || r->at > r->length || n > r->length - r->at) {
        r->ok = false;
        *length = 0;
        return NULL;
    }
    const uint8_t *p = r->data + r->at;
    r->at += n;
    *length = n;
    return p;
}
static bool end(reader_t *r) { return r->ok && r->at == r->length; }
static void put8(sftp_server_t *s, uint8_t b) {
    if (s->output_size >= sizeof s->output) {
        s->failed = true;
        return;
    }
    s->output[s->output_size++] = b;
}
static void put32(sftp_server_t *s, uint32_t v) {
    put8(s, v >> 24);
    put8(s, v >> 16);
    put8(s, v >> 8);
    put8(s, v);
}
static void put64(sftp_server_t *s, uint64_t v) {
    put32(s, (uint32_t)(v >> 32));
    put32(s, (uint32_t)v);
}
static void putstr(sftp_server_t *s, const void *data, size_t length) {
    put32(s, (uint32_t)length);
    const uint8_t *p = data;
    for (size_t i = 0; i < length; i++)
        put8(s, p[i]);
}
static void begin(sftp_server_t *s, uint8_t type, uint32_t id) {
    s->output_size = 4;
    s->output_at = 0;
    put8(s, type);
    if (type != 2)
        put32(s, id);
}
static void finish(sftp_server_t *s) {
    uint32_t n = (uint32_t)s->output_size - 4;
    s->output[0] = n >> 24;
    s->output[1] = n >> 16;
    s->output[2] = n >> 8;
    s->output[3] = n;
}
static void status(sftp_server_t *s, uint32_t id, uint32_t code) {
    const char *message = code == FX_OK                  ? "OK"
                          : code == FX_EOF               ? "EOF"
                          : code == FX_NO_SUCH_FILE      ? "Not found"
                          : code == FX_PERMISSION_DENIED ? "Permission denied"
                          : code == FX_BAD_MESSAGE       ? "Malformed request"
                          : code == FX_OP_UNSUPPORTED    ? "Unsupported operation"
                                                         : "Filesystem failure";
    begin(s, 101, id);
    put32(s, code);
    putstr(s, message, strlen(message));
    putstr(s, "", 0);
    finish(s);
}
static uint32_t error_code(int e) {
    return e == 0                             ? FX_OK
           : e == ERR_NOENT || e == ERR_NODEV ? FX_NO_SUCH_FILE
           : e == ERR_PERM                    ? FX_PERMISSION_DENIED
           : e == ERR_NOSYS                   ? FX_OP_UNSUPPORTED
                                              : FX_FAILURE;
}
static const char *const roots[] = {"sd", "usb", "usb1", "usb2", "usb3"};
static bool root_path(const char *p) { return p[0] == '/' && !p[1]; }
static int normalize(const uint8_t *data, uint32_t n, char out[VFS_PATH_MAX]) {
    size_t length = 1;
    out[0] = '/';
    out[1] = 0;
    for (uint32_t i = 0; i < n;) {
        if (!data[i])
            return ERR_PERM;
        if (data[i] == '/') {
            i++;
            continue;
        }
        uint32_t start = i;
        while (i < n && data[i] != '/') {
            /* FAT also treats backslash as a separator and colon as a drive
             * marker; reject both instead of allowing backend reinterpretation. */
            if (!data[i] || data[i] == '\\' || data[i] == ':')
                return ERR_PERM;
            i++;
        }
        size_t part = i - start;
        if (part == 1 && data[start] == '.')
            continue;
        if (part == 2 && data[start] == '.' && data[start + 1] == '.') {
            if (length == 1)
                return ERR_PERM;
            while (length > 1 && out[length - 1] != '/')
                length--;
            if (length > 1)
                length--;
            out[length] = 0;
            continue;
        }
        size_t needed = length + (length > 1) + part;
        if (needed >= VFS_PATH_MAX)
            return ERR_NAMETOOLONG;
        if (length > 1)
            out[length++] = '/';
        memcpy(out + length, data + start, part);
        length += part;
        out[length] = 0;
    }
    if (root_path(out))
        return 0;
    for (unsigned i = 0; i < sizeof roots / sizeof roots[0]; i++) {
        size_t size = strlen(roots[i]);
        if (!memcmp(out + 1, roots[i], size) && (out[size + 1] == 0 || out[size + 1] == '/'))
            return 0;
    }
    return ERR_PERM;
}
static int path(reader_t *r, char out[VFS_PATH_MAX]) {
    uint32_t n;
    const uint8_t *p = getstr(r, &n);
    return r->ok ? normalize(p, n, out) : ERR_INVAL;
}
static int mount_present(const char *p) {
    if (root_path(p))
        return 0;
    char mount[VFS_PATH_MAX];
    size_t n = 1;
    mount[0] = '/';
    while (p[n] && p[n] != '/') {
        mount[n] = p[n];
        n++;
    }
    mount[n] = 0;
    vfs_info_t info;
    int e = vfs_stat(mount, &info);
    return e ? e : info.type == FS_TYPE_DIR ? 0 : ERR_NODEV;
}
static int info(const char *p, vfs_info_t *out) {
    memset(out, 0, sizeof *out);
    if (root_path(p)) {
        out->type = FS_TYPE_DIR;
        return 0;
    }
    int e = mount_present(p);
    if (e)
        return e;
    e = vfs_stat(p, out);
    if (e)
        return e;
    return out->type == FS_TYPE_FILE || out->type == FS_TYPE_DIR ? 0 : ERR_PERM;
}
static void attributes(sftp_server_t *s, const vfs_info_t *entry) {
    bool directory = entry->type == FS_TYPE_DIR;
    put32(s, ATTR_SIZE | ATTR_PERMISSIONS);
    put64(s, directory ? 0 : entry->size);
    /* These mode bits express server policy, not stored POSIX metadata. */
    put32(s, (directory ? 0040000u : 0100000u) |
                 (directory ? (s->writable ? 0777u : 0555u) : (s->writable ? 0666u : 0444u)));
}
static bool skip_attributes(reader_t *r) {
    uint32_t flags = get32(r);
    if (flags & ~(ATTR_SIZE | ATTR_UIDGID | ATTR_PERMISSIONS | ATTR_ACMODTIME | ATTR_EXTENDED))
        return false;
    if (flags & ATTR_SIZE)
        (void)get64(r);
    if (flags & ATTR_UIDGID) {
        (void)get32(r);
        (void)get32(r);
    }
    if (flags & ATTR_PERMISSIONS)
        (void)get32(r);
    if (flags & ATTR_ACMODTIME) {
        (void)get32(r);
        (void)get32(r);
    }
    if (flags & ATTR_EXTENDED) {
        uint32_t count = get32(r);
        if (count > 16)
            return false;
        for (uint32_t i = 0; r->ok && i < count; i++) {
            uint32_t n;
            (void)getstr(r, &n);
            (void)getstr(r, &n);
        }
    }
    return r->ok;
}
static handle_t *find_handle(sftp_server_t *s, reader_t *r) {
    uint32_t n;
    const uint8_t *p = getstr(r, &n);
    if (!r->ok || n != 4)
        return NULL;
    uint32_t token = load32(p);
    for (unsigned i = 0; i < SFTP_SERVER_HANDLES_MAX; i++)
        if (s->handles[i].used && s->handles[i].token == token)
            return &s->handles[i];
    return NULL;
}
static handle_t *new_handle(sftp_server_t *s) {
    if (s->serial == UINT32_MAX)
        return NULL;
    for (unsigned i = 0; i < SFTP_SERVER_HANDLES_MAX; i++)
        if (!s->handles[i].used) {
            handle_t *h = &s->handles[i];
            memset(h, 0, sizeof *h);
            h->used = true;
            h->fd = -1;
            h->token = ++s->serial;
            return h;
        }
    return NULL;
}
static void handle_reply(sftp_server_t *s, uint32_t id, handle_t *h) {
    begin(s, 102, id);
    put32(s, 4);
    put32(s, h->token);
    finish(s);
}
static void name_reply(sftp_server_t *s, uint32_t id, const char *name, const vfs_info_t *entry) {
    begin(s, 104, id);
    put32(s, 1);
    putstr(s, name, strlen(name));
    putstr(s, name, strlen(name));
    attributes(s, entry);
    finish(s);
}
static int close_handle(handle_t *h) {
    int e = h->directory ? 0 : vfs_close(h->fd);
    h->used = false;
    return e;
}
static int readdir_one(const char *, uint32_t, vfs_info_t *);
static void process(sftp_server_t *s) {
    const uint8_t *packet = s->input + 4;
    size_t n = s->input_need - 4;
    if (!n) {
        s->failed = true;
        return;
    }
    uint8_t type = packet[0];
    reader_t r = {packet, n, 1, true};
    if (type == 1) {
        uint32_t version = get32(&r);
        while (r.ok && r.at < r.length) {
            uint32_t z;
            (void)getstr(&r, &z);
            (void)getstr(&r, &z);
        }
        if (s->initialized || version < 3 || !end(&r)) {
            s->failed = true;
            return;
        }
        s->initialized = true;
        begin(s, 2, 0);
        put32(s, 3);
        finish(s);
        return;
    }
    uint32_t id = get32(&r);
    if (!s->initialized || !r.ok) {
        s->failed = true;
        return;
    }
    char pathname[VFS_PATH_MAX];
    vfs_info_t entry;
    int e = 0;
    if (type == 3 || type == 7 || type == 16 || type == 17 || type == 11 || type == 13 ||
        type == 14 || type == 15) {
        e = path(&r, pathname);
        uint32_t flags = type == 3 ? get32(&r) : 0;
        bool attrs = type != 3 && type != 14 ? true : skip_attributes(&r);
        if (!end(&r) || !attrs) {
            status(s, id, FX_BAD_MESSAGE);
            return;
        }
        if (e) {
            status(s, id, error_code(e));
            return;
        }
        if (type == 16) {
            e = info(pathname, &entry);
            if (e)
                status(s, id, error_code(e));
            else
                name_reply(s, id, pathname, &entry);
            return;
        }
        if (type == 7 || type == 17) {
            e = info(pathname, &entry);
            if (e)
                status(s, id, error_code(e));
            else {
                begin(s, 105, id);
                attributes(s, &entry);
                finish(s);
            }
            return;
        }
        if (type == 11) {
            e = info(pathname, &entry);
            if (e || entry.type != FS_TYPE_DIR) {
                status(s, id, error_code(e ? e : ERR_NOTDIR));
                return;
            }
            handle_t *h = new_handle(s);
            if (!h) {
                status(s, id, FX_FAILURE);
                return;
            }
            h->directory = true;
            h->root = root_path(pathname);
            strcpy(h->path, pathname);
            handle_reply(s, id, h);
            return;
        }
        if (type == 3) {
            if (flags & ~63u || !(flags & (P_READ | P_WRITE)) ||
                ((flags & (P_CREATE | P_TRUNC | P_APPEND)) && !(flags & P_WRITE))) {
                status(s, id, FX_BAD_MESSAGE);
                return;
            }
            if ((flags & P_EXCL) && !(flags & P_CREATE)) {
                status(s, id, FX_BAD_MESSAGE);
                return;
            }
            if ((flags & (P_WRITE | P_CREATE | P_TRUNC | P_APPEND)) && !s->writable) {
                status(s, id, FX_PERMISSION_DENIED);
                return;
            }
            if (root_path(pathname)) {
                status(s, id, FX_PERMISSION_DENIED);
                return;
            }
            e = mount_present(pathname);
            if (e) {
                status(s, id, error_code(e));
                return;
            }
            e = info(pathname, &entry);
            if (!e && entry.type != FS_TYPE_FILE) {
                status(s, id, FX_FAILURE);
                return;
            }
            if (e && !(e == ERR_NOENT && (flags & P_CREATE))) {
                status(s, id, error_code(e));
                return;
            }
            handle_t *h = new_handle(s);
            if (!h) {
                status(s, id, FX_FAILURE);
                return;
            }
            uint32_t vf = (flags & P_READ ? VFS_O_READ : 0) | (flags & P_WRITE ? VFS_O_WRITE : 0) |
                          (flags & P_APPEND ? VFS_O_APPEND : 0) |
                          (flags & P_CREATE ? VFS_O_CREATE : 0) |
                          (flags & P_TRUNC ? VFS_O_TRUNC : 0) | (flags & P_EXCL ? VFS_O_EXCL : 0);
            h->fd = vfs_open(pathname, vf);
            if (h->fd < 0) {
                h->used = false;
                status(s, id, error_code(h->fd));
                return;
            }
            h->flags = flags;
            strcpy(h->path, pathname);
            handle_reply(s, id, h);
            return;
        }
        if (!s->writable) {
            status(s, id, FX_PERMISSION_DENIED);
            return;
        }
        /* Never remove a mount root or the virtual root. */
        const char *slash = strchr(pathname + 1, '/');
        if (!slash) {
            status(s, id, FX_PERMISSION_DENIED);
            return;
        }
        e = mount_present(pathname);
        if (e) {
            status(s, id, error_code(e));
            return;
        }
        if (type == 14)
            e = vfs_mkdir(pathname);
        else {
            e = info(pathname, &entry);
            if (!e && ((type == 13 && entry.type != FS_TYPE_FILE) ||
                       (type == 15 && entry.type != FS_TYPE_DIR)))
                e = ERR_INVAL;
            if (!e)
                e = vfs_unlink(pathname);
        }
        status(s, id, error_code(e));
        return;
    }
    if (type == 4 || type == 5 || type == 6 || type == 8 || type == 12) {
        handle_t *h = find_handle(s, &r);
        uint64_t offset = 0;
        uint32_t count = 0;
        const uint8_t *data = NULL;
        if (type == 5 || type == 6)
            offset = get64(&r);
        if (type == 5)
            count = get32(&r);
        if (type == 6)
            data = getstr(&r, &count);
        if (!end(&r)) {
            status(s, id, FX_BAD_MESSAGE);
            return;
        }
        if (!h) {
            status(s, id, FX_FAILURE);
            return;
        }
        if (type == 4) {
            status(s, id, error_code(close_handle(h)));
            return;
        }
        e = mount_present(h->path);
        if (e) {
            status(s, id, error_code(e));
            return;
        }
        if (h->directory) {
            if (type != 12 && type != 8) {
                status(s, id, FX_FAILURE);
                return;
            }
        } else if (type == 12) {
            status(s, id, FX_FAILURE);
            return;
        }
        if (type == 8) {
            if (h->directory) {
                e = info(h->path, &entry);
            } else {
                int current = vfs_seek(h->fd, 0, FS_SEEK_CUR);
                if (current < 0)
                    e = current;
                else {
                    int length = vfs_seek(h->fd, 0, FS_SEEK_END);
                    int restored = vfs_seek(h->fd, current, FS_SEEK_SET);
                    if (length < 0)
                        e = length;
                    else if (restored != current)
                        e = restored < 0 ? restored : ERR_IO;
                    else {
                        memset(&entry, 0, sizeof entry);
                        entry.type = FS_TYPE_FILE;
                        entry.size = (uint32_t)length;
                    }
                }
            }
            if (e)
                status(s, id, error_code(e));
            else {
                begin(s, 105, id);
                attributes(s, &entry);
                finish(s);
            }
            return;
        }
        if (type == 5 || type == 6) {
            if (h->directory || (type == 5 && !(h->flags & P_READ)) ||
                (type == 6 && (!(h->flags & P_WRITE) || !s->writable))) {
                status(s, id, FX_PERMISSION_DENIED);
                return;
            }
            if (type == 6 && (h->flags & P_APPEND)) {
                int length = vfs_seek(h->fd, 0, FS_SEEK_END);
                if (length < 0) {
                    status(s, id, error_code(length));
                    return;
                }
                offset = (uint32_t)length;
            }
            if (type == 5 && count > SFTP_SERVER_READ_MAX)
                count = SFTP_SERVER_READ_MAX;
            if (offset > INT32_MAX || count > INT32_MAX - offset) {
                status(s, id, FX_FAILURE);
                return;
            }
            e = vfs_seek(h->fd, (int32_t)offset, FS_SEEK_SET);
            if (e < 0 || (uint64_t)e != offset) {
                status(s, id, error_code(e < 0 ? e : ERR_IO));
                return;
            }
            if (type == 5) {
                begin(s, 103, id);
                put32(s, 0);
                e = vfs_read(h->fd, s->output + s->output_size, count);
                if (e < 0) {
                    status(s, id, error_code(e));
                    return;
                }
                if ((uint32_t)e > count) {
                    status(s, id, FX_FAILURE);
                    return;
                }
                if (!e && count) {
                    status(s, id, FX_EOF);
                    return;
                }
                s->output[9] = (uint32_t)e >> 24;
                s->output[10] = (uint32_t)e >> 16;
                s->output[11] = (uint32_t)e >> 8;
                s->output[12] = (uint32_t)e;
                s->output_size += (size_t)e;
                finish(s);
                return;
            }
            size_t done = 0;
            while (done < count) {
                e = vfs_write(h->fd, data + done, count - done);
                if (e <= 0 || (size_t)e > count - done) {
                    status(s, id, error_code(e < 0 ? e : ERR_IO));
                    return;
                }
                done += (size_t)e;
            }
            status(s, id, FX_OK);
            return;
        }
        /* READDIR: one entry per response keeps memory bounded. */
        e = info(h->path, &entry);
        if (e) {
            status(s, id, error_code(e));
            return;
        }
        if (h->root) {
            while (h->cursor < sizeof roots / sizeof roots[0]) {
                const char *name = roots[h->cursor++];
                char p[8] = "/";
                strcpy(p + 1, name);
                if (!info(p, &entry) && entry.type == FS_TYPE_DIR) {
                    name_reply(s, id, name, &entry);
                    return;
                }
            }
            status(s, id, FX_EOF);
            return;
        }
        /* Implemented by the bounded readdir selector below. */
        e = readdir_one(h->path, h->cursor, &entry);
        if (e > 0) {
            h->cursor++;
            name_reply(s, id, entry.name, &entry);
        } else
            status(s, id, e < 0 ? error_code(e) : FX_EOF);
        return;
    }
    status(s, id, FX_OP_UNSUPPORTED);
}
typedef struct {
    uint32_t skip, seen;
    vfs_info_t *out;
    bool found;
} directory_select_t;
static int select_entry(const vfs_info_t *entry, void *arg) {
    directory_select_t *d = arg;
    if (entry->type != FS_TYPE_FILE && entry->type != FS_TYPE_DIR)
        return 0;
    size_t n = 0;
    while (n < sizeof entry->name && entry->name[n])
        n++;
    if (!n || n == sizeof entry->name || strchr(entry->name, '/') || !strcmp(entry->name, ".") ||
        !strcmp(entry->name, ".."))
        return 0;
    if (d->seen++ < d->skip)
        return 0;
    *d->out = *entry;
    d->found = true;
    return 1;
}
static int readdir_one(const char *path, uint32_t skip, vfs_info_t *out) {
    directory_select_t d = {skip, 0, out, false};
    int e = vfs_readdir(path, select_entry, &d);
    return e < 0 ? e : d.found ? 1 : 0;
}
sftp_server_t *sftp_server_create(bool writable) {
    sftp_server_t *s = kmalloc_psram(sizeof *s);
    if (!s)
        return NULL;
    memset(s, 0, sizeof *s);
    s->writable = writable;
    s->input_need = 4;
    return s;
}
size_t sftp_server_feed(sftp_server_t *s, const void *data, size_t length) {
    if (!s || s->failed || (!data && length) || s->output_size > s->output_at)
        return 0;
    const uint8_t *p = data;
    size_t used = 0;
    while (used < length && !s->failed && !s->output_size) {
        size_t n = s->input_need - s->input_used;
        if (n > length - used)
            n = length - used;
        memcpy(s->input + s->input_used, p + used, n);
        s->input_used += n;
        used += n;
        if (s->input_used < s->input_need)
            continue;
        if (s->input_need == 4) {
            uint32_t packet = load32(s->input);
            if (!packet || packet > SFTP_SERVER_PACKET_MAX) {
                s->failed = true;
                break;
            }
            s->input_need = (size_t)packet + 4;
            continue;
        }
        process(s);
        s->input_used = 0;
        s->input_need = 4;
    }
    return used;
}
size_t sftp_server_read(sftp_server_t *s, void *data, size_t capacity) {
    if (!s || !capacity || !data)
        return 0;
    size_t n = s->output_size - s->output_at;
    if (n > capacity)
        n = capacity;
    memcpy(data, s->output + s->output_at, n);
    s->output_at += n;
    if (s->output_at == s->output_size)
        s->output_at = s->output_size = 0;
    return n;
}
bool sftp_server_failed(const sftp_server_t *s) { return !s || s->failed; }
int sftp_server_destroy(sftp_server_t *s) {
    if (!s)
        return 0;
    int first = 0;
    for (unsigned i = 0; i < SFTP_SERVER_HANDLES_MAX; i++)
        if (s->handles[i].used) {
            int e = close_handle(&s->handles[i]);
            if (!first && e < 0)
                first = e;
        }
    memset(s, 0, sizeof *s);
    kfree(s);
    return first;
}
