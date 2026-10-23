/* MeOS streaming VFS adapter for MicroPython1.26.1. One active VM owns it.
 * Explicit FD registry teardown is required before the VM heap is released;
 * GC finalizers are deliberately not relied upon for resource ownership. */
#if defined(M5OS_MICROPYTHON) && M5OS_MICROPYTHON
#include "py/builtin.h"
#include "py/lexer.h"
#include "py/nlr.h"
#include "py/objstr.h"
#include "py/reader.h"
#include "py/runtime.h"
#include "py/stream.h"
#include <limits.h>
#include <m5os/errno.h>
#include <m5os/kmalloc.h>
#include <m5os/vfs.h>
#include <string.h>
#define MEOS_MP_FILES 8u
#define MEOS_MP_READER_BUFFER 128u
typedef struct {
    bool used, reader, eof;
    uint32_t token, flags;
    int fd;
    size_t at, length;
    uint8_t buffer[MEOS_MP_READER_BUFFER];
} meos_slot_t;
typedef struct {
    mp_obj_base_t base;
    unsigned slot;
    uint32_t token;
} meos_file_t;
static meos_slot_t *slots;
static uint32_t next_token;
static int deferred_close_error;
static char current_directory[VFS_PATH_MAX] = "/";
static const mp_obj_type_t meos_binary_file_type, meos_text_file_type;
static int positive_errno(int error) {
    switch (error) {
    case ERR_NODEV:
        return MP_ENODEV;
    case ERR_BADF:
        return MP_EBADF;
    case ERR_INVAL:
        return MP_EINVAL;
    case ERR_NOSYS:
        return MP_EOPNOTSUPP;
    case ERR_BUSY:
        return MP_EBUSY;
    case ERR_NOMEM:
        return MP_ENOMEM;
    case ERR_EXIST:
        return MP_EEXIST;
    case ERR_NOTDIR:
        return MP_ENOTDIR;
    case ERR_ISDIR:
        return MP_EISDIR;
    case ERR_NOSPC:
        return MP_ENOSPC;
    case ERR_NAMETOOLONG:
        return MP_EINVAL;
    case ERR_NOENT:
        return MP_ENOENT;
    case ERR_NOTEMPTY:
        return MP_EEXIST;
    case ERR_PERM:
        return MP_EACCES;
    case ERR_TIMEOUT:
        return MP_ETIMEDOUT;
    default:
        return MP_EIO;
    }
}
static int absolute_path(const char *input, size_t length, char output[VFS_PATH_MAX]) {
    if (!input || !length || length >= VFS_PATH_MAX || memchr(input, 0, length))
        return ERR_INVAL;
    char path[VFS_PATH_MAX];
    size_t prefix = input[0] == '/' ? 0 : strlen(current_directory);
    bool separator = prefix && current_directory[prefix - 1] != '/';
    if (prefix + separator + length >= sizeof path)
        return ERR_NAMETOOLONG;
    if (prefix)
        memcpy(path, current_directory, prefix);
    if (separator)
        path[prefix++] = '/';
    memcpy(path + prefix, input, length);
    path[prefix + length] = 0;
    return vfs_normalize(path, output);
}
int meos_vfs_set_cwd(const char *path) {
    if (!path || path[0] != '/')
        return ERR_INVAL;
    char normalized[VFS_PATH_MAX];
    int e = vfs_normalize(path, normalized);
    if (e)
        return e;
    vfs_info_t info;
    e = vfs_stat(normalized, &info);
    if (e)
        return e;
    if (info.type != FS_TYPE_DIR)
        return ERR_NOTDIR;
    strcpy(current_directory, normalized);
    return 0;
}
static int slot_close(meos_slot_t *slot) {
    if (!slot->used)
        return 0;
    int fd = slot->fd;
    slot->used = false;
    slot->fd = -1;
    slot->length = slot->at = 0;
    return vfs_close(fd); /* VFS consumes the FD even when close/flush reports failure. */
}
int meos_vfs_deinit(void) {
    int first = deferred_close_error;
    deferred_close_error = 0;
    for (unsigned i = 0; slots && i < MEOS_MP_FILES; i++)
        if (slots[i].used) {
            int e = slot_close(&slots[i]);
            if (!first && e < 0)
                first = e;
        }
    kfree(slots);
    slots = NULL;
    return first;
}
void meos_vfs_init(void) {
    (void)meos_vfs_deinit();
    slots = kmalloc_psram(sizeof(*slots) * MEOS_MP_FILES);
    if (!slots)
        mp_raise_OSError(MP_ENOMEM);
    memset(slots, 0, sizeof(*slots) * MEOS_MP_FILES);
    strcpy(current_directory, "/");
    /* next_token is not reset: stale objects cannot claim a later VM's FD. */
}
static int slot_open(const char *path, uint32_t flags, bool reader) {
    if (!slots)
        mp_raise_OSError(MP_EBADF);
    unsigned index = MEOS_MP_FILES;
    for (unsigned i = 0; i < MEOS_MP_FILES; i++)
        if (!slots[i].used) {
            index = i;
            break;
        }
    if (index == MEOS_MP_FILES || next_token == UINT32_MAX)
        mp_raise_OSError(MP_EMFILE);
    int fd = vfs_open(path, flags);
    if (fd < 0)
        mp_raise_OSError(positive_errno(fd));
    meos_slot_t *slot = &slots[index];
    memset(slot, 0, sizeof *slot);
    slot->used = true;
    slot->fd = fd;
    slot->flags = flags;
    slot->reader = reader;
    slot->token = ++next_token;
    return (int)index;
}
static meos_slot_t *file_slot(meos_file_t *file) {
    if (!slots || file->slot >= MEOS_MP_FILES)
        return NULL;
    meos_slot_t *slot = &slots[file->slot];
    return slot->used && !slot->reader && slot->token == file->token ? slot : NULL;
}
static mp_uint_t file_read(mp_obj_t self_in, void *data, mp_uint_t size, int *error) {
    meos_slot_t *slot = file_slot(MP_OBJ_TO_PTR(self_in));
    if (!slot) {
        *error = MP_EBADF;
        return MP_STREAM_ERROR;
    }
    if (!(slot->flags & VFS_O_READ)) {
        *error = MP_EACCES;
        return MP_STREAM_ERROR;
    }
    if (size > INT32_MAX)
        size = INT32_MAX;
    int result = vfs_read(slot->fd, data, size);
    if (result < 0) {
        *error = positive_errno(result);
        return MP_STREAM_ERROR;
    }
    if ((mp_uint_t)result > size) {
        *error = MP_EIO;
        return MP_STREAM_ERROR;
    }
    return (mp_uint_t)result;
}
static mp_uint_t file_write(mp_obj_t self_in, const void *data, mp_uint_t size, int *error) {
    meos_slot_t *slot = file_slot(MP_OBJ_TO_PTR(self_in));
    if (!slot) {
        *error = MP_EBADF;
        return MP_STREAM_ERROR;
    }
    if (!(slot->flags & VFS_O_WRITE)) {
        *error = MP_EACCES;
        return MP_STREAM_ERROR;
    }
    if (size > INT32_MAX)
        size = INT32_MAX;
    int result = vfs_write(slot->fd, data, size);
    if (result < 0) {
        *error = positive_errno(result);
        return MP_STREAM_ERROR;
    }
    if ((mp_uint_t)result > size || (size && !result)) {
        *error = MP_EIO;
        return MP_STREAM_ERROR;
    }
    return (mp_uint_t)result;
}
static mp_uint_t file_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *error) {
    meos_file_t *file = MP_OBJ_TO_PTR(self_in);
    meos_slot_t *slot = file_slot(file);
    if (request == MP_STREAM_CLOSE) {
        if (!slot)
            return 0;
        int e = slot_close(slot);
        file->token = 0;
        if (e < 0) {
            *error = positive_errno(e);
            return MP_STREAM_ERROR;
        }
        return 0;
    }
    if (!slot) {
        *error = MP_EBADF;
        return MP_STREAM_ERROR;
    }
    if (request == MP_STREAM_SEEK) {
        struct mp_stream_seek_t *seek = (struct mp_stream_seek_t *)arg;
        if (!seek || seek->whence < FS_SEEK_SET || seek->whence > FS_SEEK_END ||
            seek->offset < INT32_MIN || seek->offset > INT32_MAX) {
            *error = MP_EINVAL;
            return MP_STREAM_ERROR;
        }
        int result = vfs_seek(slot->fd, (int32_t)seek->offset, seek->whence);
        if (result < 0) {
            *error = positive_errno(result);
            return MP_STREAM_ERROR;
        }
        seek->offset = result;
        return 0;
    }
    if (request == MP_STREAM_GET_FILENO)
        return (mp_uint_t)slot->fd;
    if (request == MP_STREAM_FLUSH) {
        /* The VFS has no generic fsync ioctl. Reads need no flush; writable
         * files synchronize on close (and current FAT writes sync each call). */
        if (!(slot->flags & VFS_O_WRITE))
            return 0;
        *error = MP_EOPNOTSUPP;
        return MP_STREAM_ERROR;
    }
    *error = MP_EINVAL;
    return MP_STREAM_ERROR;
}
static const mp_rom_map_elem_t file_locals_table[] = {
    {MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read_obj)},
    {MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj)},
    {MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj)},
    {MP_ROM_QSTR(MP_QSTR_readlines), MP_ROM_PTR(&mp_stream_unbuffered_readlines_obj)},
    {MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_stream_write_obj)},
    {MP_ROM_QSTR(MP_QSTR_seek), MP_ROM_PTR(&mp_stream_seek_obj)},
    {MP_ROM_QSTR(MP_QSTR_tell), MP_ROM_PTR(&mp_stream_tell_obj)},
    {MP_ROM_QSTR(MP_QSTR_flush), MP_ROM_PTR(&mp_stream_flush_obj)},
    {MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&mp_stream_close_obj)},
    {MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&mp_identity_obj)},
    {MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&mp_stream___exit___obj)},
};
static MP_DEFINE_CONST_DICT(file_locals, file_locals_table);
static const mp_stream_p_t binary_stream = {
    .read = file_read, .write = file_write, .ioctl = file_ioctl};
static const mp_stream_p_t text_stream = {
    .read = file_read, .write = file_write, .ioctl = file_ioctl, .is_text = true};
static MP_DEFINE_CONST_OBJ_TYPE(meos_binary_file_type, MP_QSTR_FileIO, MP_TYPE_FLAG_ITER_IS_STREAM,
                                protocol, &binary_stream, locals_dict, &file_locals);
static MP_DEFINE_CONST_OBJ_TYPE(meos_text_file_type, MP_QSTR_TextIOWrapper,
                                MP_TYPE_FLAG_ITER_IS_STREAM, protocol, &text_stream, locals_dict,
                                &file_locals);
static bool mode_flags(const char *mode, size_t length, uint32_t *flags, bool *binary) {
    if (!length || length > 4)
        return false;
    bool plus = false, kind = false, format = false;
    uint32_t value = 0;
    *binary = false;
    for (size_t i = 0; i < length; i++)
        switch (mode[i]) {
        case 'r':
        case 'w':
        case 'a':
        case 'x':
            if (kind)
                return false;
            kind = true;
            value |= mode[i] == 'r' ? VFS_O_READ : VFS_O_WRITE | VFS_O_CREATE;
            if (mode[i] == 'w')
                value |= VFS_O_TRUNC;
            if (mode[i] == 'a')
                value |= VFS_O_APPEND;
            if (mode[i] == 'x')
                value |= VFS_O_EXCL;
            break;
        case '+':
            if (plus)
                return false;
            plus = true;
            break;
        case 'b':
        case 't':
            if (format)
                return false;
            format = true;
            *binary = mode[i] == 'b';
            break;
        default:
            return false;
        }
    if (!kind)
        return false;
    if (plus)
        value |= VFS_O_READ | VFS_O_WRITE;
    *flags = value;
    return true;
}
mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    enum { ARG_file, ARG_mode };
    static const mp_arg_t allowed[] = {
        {MP_QSTR_file, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
        {MP_QSTR_mode, MP_ARG_OBJ, {.u_obj = MP_OBJ_NEW_QSTR(MP_QSTR_r)}},
    };
    mp_arg_val_t values[2];
    mp_arg_parse_all(n_args, args, kwargs, 2, allowed, values);
    size_t length;
    const char *mode = mp_obj_str_get_data(values[ARG_mode].u_obj, &length);
    uint32_t flags;
    bool binary;
    if (!mode_flags(mode, length, &flags, &binary))
        mp_raise_ValueError(MP_ERROR_TEXT("invalid file mode"));
    const char *filename = mp_obj_str_get_data(values[ARG_file].u_obj, &length);
    char path[VFS_PATH_MAX];
    int e = absolute_path(filename, length, path);
    if (e)
        mp_raise_OSError(positive_errno(e));
    /* Allocate before opening, so a GC allocation exception never leaks an FD. */
    meos_file_t *file =
        mp_obj_malloc(meos_file_t, binary ? &meos_binary_file_type : &meos_text_file_type);
    file->slot = MEOS_MP_FILES;
    file->token = 0;
    int index = slot_open(path, flags, false);
    file->slot = (unsigned)index;
    file->token = slots[index].token;
    return MP_OBJ_FROM_PTR(file);
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, mp_builtin_open);
mp_import_stat_t mp_import_stat(const char *path) {
    char normalized[VFS_PATH_MAX];
    vfs_info_t info;
    if (!path || absolute_path(path, strlen(path), normalized) || vfs_stat(normalized, &info))
        return MP_IMPORT_STAT_NO_EXIST;
    return info.type == FS_TYPE_DIR    ? MP_IMPORT_STAT_DIR
           : info.type == FS_TYPE_FILE ? MP_IMPORT_STAT_FILE
                                       : MP_IMPORT_STAT_NO_EXIST;
}
static meos_slot_t *reader_slot(void *data) {
    uint32_t token = (uint32_t)(uintptr_t)data;
    for (unsigned i = 0; i < MEOS_MP_FILES; i++)
        if (slots && slots[i].used && slots[i].reader && slots[i].token == token)
            return &slots[i];
    return NULL;
}
static mp_uint_t reader_byte(void *data) {
    meos_slot_t *slot = reader_slot(data);
    if (!slot || slot->eof)
        return MP_READER_EOF;
    if (slot->at == slot->length) {
        int n = vfs_read(slot->fd, slot->buffer, sizeof slot->buffer);
        if (n < 0 || (unsigned)n > sizeof slot->buffer) {
            int error = n < 0 ? positive_errno(n) : MP_EIO;
            int close_error = slot_close(slot);
            if (close_error < 0 && !deferred_close_error)
                deferred_close_error = close_error;
            mp_raise_OSError(error);
        }
        slot->at = 0;
        slot->length = (size_t)n;
        if (!n) {
            slot->eof = true;
            return MP_READER_EOF;
        }
    }
    return slot->buffer[slot->at++];
}
static void reader_close(void *data) {
    meos_slot_t *slot = reader_slot(data);
    if (!slot)
        return;
    int e = slot_close(slot);
    if (e < 0 && !deferred_close_error)
        deferred_close_error = e;
}
void mp_reader_new_file(mp_reader_t *reader, qstr filename) {
    char path[VFS_PATH_MAX];
    int e = absolute_path(qstr_str(filename), qstr_len(filename), path);
    if (e)
        mp_raise_OSError(positive_errno(e));
    int index = slot_open(path, VFS_O_READ, true);
    reader->data = (void *)(uintptr_t)slots[index].token;
    reader->readbyte = reader_byte;
    reader->close = reader_close;
}
mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    mp_reader_t reader;
    mp_reader_new_file(&reader, filename);
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lexer = mp_lexer_new(filename, reader);
        nlr_pop();
        return lexer;
    }
    /* Lexer allocation or its initial reads can raise before a lexer exists
     * for parser cleanup. Token-based close is safe even after read failure. */
    reader.close(reader.data);
    nlr_jump(nlr.ret_val);
}
#endif
