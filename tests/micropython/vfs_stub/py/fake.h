#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <assert.h>
#include <errno.h>
typedef uintptr_t mp_uint_t;typedef intptr_t mp_int_t;typedef int64_t mp_off_t;
typedef void *mp_obj_t;typedef const char *qstr;typedef struct {int unused;} mp_obj_type_t;
typedef struct {const mp_obj_type_t *type;} mp_obj_base_t;typedef struct {int unused;} mp_map_t;
typedef union {mp_obj_t u_obj;int u_int;} mp_arg_val_t;
typedef struct {qstr name;int flags;mp_arg_val_t defval;} mp_arg_t;
typedef struct {uintptr_t key,value;} mp_rom_map_elem_t;
#define MP_ARG_REQUIRED 1
#define MP_ARG_OBJ 2
#define MP_OBJ_NULL NULL
#define MP_OBJ_TO_PTR(p) (p)
#define MP_OBJ_FROM_PTR(p) ((void *)(p))
#define MP_OBJ_NEW_QSTR(p) ((void *)(p))
#define MP_ERROR_TEXT(s) (s)
#define MP_ROM_QSTR(p) 0
#define MP_ROM_PTR(p) ((uintptr_t)(p))
#define MP_TYPE_FLAG_ITER_IS_STREAM 1
#define MP_DEFINE_CONST_DICT(name,table) const int name=0
#define MP_DEFINE_CONST_OBJ_TYPE(name,...) const mp_obj_type_t name={0}
#define MP_DEFINE_CONST_FUN_OBJ_KW(name,...) const int name=0
static jmp_buf failjmp;static int raised;static bool alloc_fail;
typedef struct fake_nlr {struct fake_nlr *prev;void *ret_val;jmp_buf jmpbuf;} nlr_buf_t;
static nlr_buf_t *nlr_active;
#define nlr_push(b) ((b)->prev=nlr_active,nlr_active=(b),setjmp((b)->jmpbuf))
static void nlr_pop(void) {nlr_active=nlr_active->prev;}
static _Noreturn void nlr_jump(void *value) {if(nlr_active){nlr_buf_t *b=nlr_active;nlr_active=b->prev;b->ret_val=value;longjmp(b->jmpbuf,1);}raised=(int)(uintptr_t)value;longjmp(failjmp,1);}
static _Noreturn void mp_raise_OSError(int e) {nlr_jump((void *)(uintptr_t)e);}
static _Noreturn void mp_raise_ValueError(const char *s) {(void)s;mp_raise_OSError(EINVAL);}
static const char *mp_obj_str_get_data(mp_obj_t o,size_t *n) {*n=strlen(o);return o;}
static void *fake_alloc(size_t n,const mp_obj_type_t *type) {if(alloc_fail)mp_raise_OSError(ENOMEM);mp_obj_base_t *p=calloc(1,n);p->type=type;return p;}
#define mp_obj_malloc(t,ty) ((t *)fake_alloc(sizeof(t),ty))
static void mp_arg_parse_all(size_t n,const mp_obj_t *a,mp_map_t *kw,size_t count,const mp_arg_t *allowed,mp_arg_val_t *out) {(void)kw;assert(n&&n<=count);for(size_t i=0;i<count;i++)out[i]=i<n?(mp_arg_val_t){.u_obj=a[i]}:allowed[i].defval;}
#define qstr_str(q) (q)
#define qstr_len(q) strlen(q)
typedef struct {void *data;mp_uint_t (*readbyte)(void *);void (*close)(void *);} mp_reader_t;
typedef struct {mp_reader_t reader;} mp_lexer_t;
static bool lexer_fail;
static mp_lexer_t *mp_lexer_new(qstr q,mp_reader_t r) {(void)q;if(lexer_fail)mp_raise_OSError(ENOMEM);mp_lexer_t *l=malloc(sizeof *l);l->reader=r;return l;}
typedef enum {MP_IMPORT_STAT_NO_EXIST,MP_IMPORT_STAT_DIR,MP_IMPORT_STAT_FILE} mp_import_stat_t;
#define MP_STREAM_ERROR ((mp_uint_t)-1)
#define MP_READER_EOF ((mp_uint_t)-1)
#define MP_STREAM_FLUSH 1
#define MP_STREAM_SEEK 2
#define MP_STREAM_CLOSE 4
#define MP_STREAM_GET_FILENO 10
struct mp_stream_seek_t {mp_off_t offset;int whence;};
typedef struct {mp_uint_t (*read)(mp_obj_t,void *,mp_uint_t,int *);mp_uint_t (*write)(mp_obj_t,const void *,mp_uint_t,int *);mp_uint_t (*ioctl)(mp_obj_t,mp_uint_t,uintptr_t,int *);unsigned is_text:1;} mp_stream_p_t;
static const int mp_stream_read_obj,mp_stream_readinto_obj,mp_stream_unbuffered_readline_obj,mp_stream_unbuffered_readlines_obj,mp_stream_write_obj,mp_stream_seek_obj,mp_stream_tell_obj,mp_stream_flush_obj,mp_stream_close_obj,mp_identity_obj,mp_stream___exit___obj;
#define MP_ENODEV ENODEV
#define MP_EBADF EBADF
#define MP_EINVAL EINVAL
#define MP_EOPNOTSUPP EOPNOTSUPP
#define MP_EBUSY EBUSY
#define MP_ENOMEM ENOMEM
#define MP_EEXIST EEXIST
#define MP_ENOTDIR ENOTDIR
#define MP_EISDIR EISDIR
#define MP_ENOSPC ENOSPC
#define MP_ENOENT ENOENT
#define MP_EACCES EACCES
#define MP_ETIMEDOUT ETIMEDOUT
#define MP_EIO EIO
#define MP_EMFILE EMFILE
#define MP_QSTR_read "read"
#define MP_QSTR_readinto "readinto"
#define MP_QSTR_readline "readline"
#define MP_QSTR_readlines "readlines"
#define MP_QSTR_write "write"
#define MP_QSTR_seek "seek"
#define MP_QSTR_tell "tell"
#define MP_QSTR_flush "flush"
#define MP_QSTR_close "close"
#define MP_QSTR___enter__ "__enter__"
#define MP_QSTR___exit__ "__exit__"
#define MP_QSTR_FileIO "FileIO"
#define MP_QSTR_TextIOWrapper "TextIOWrapper"
#define MP_QSTR_file "file"
#define MP_QSTR_mode "mode"
#define MP_QSTR_r "r"
