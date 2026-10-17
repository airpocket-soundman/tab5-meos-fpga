/*
 * M5FS - a deliberately small filesystem.  See m5fs.h for the on-disk format.
 *
 * In RAM we keep: the superblock, the whole allocation bitmap and the whole
 * directory table (8 KiB).  Metadata edits only mark the 512-byte blocks they
 * touch as dirty; fs_close()/fs_sync() write just those.  File data goes
 * straight to the device (whole blocks directly, partial blocks through one
 * bounce buffer), so there is no data cache to lose.
 *
 * One lock (mutex) serializes every public call.
 */
#include <m5os/fs.h>
#include <m5os/errno.h>
#include <m5os/kmalloc.h>
#include <m5os/mutex.h>
#include <m5os/crc32.h>
#include <m5os/kprintf.h>
#include "m5fs.h"

#define FD_MAX        8
#define BITS_PER_BLOCK (M5FS_BLOCK_SIZE * 8u)
#define IDX_ROOT      (-2)                  /* lookup(): the path was "/" */
#define IDX_NONE      (-1)                  /* lookup(): last component does not exist */

typedef struct { bool used; uint8_t ent; uint32_t pos; uint32_t flags; } fd_t;

static struct {
    bool           mounted;
    blockdev_t    *bd;
    uint32_t       base;                    /* volume start on the device */
    m5fs_super_t   sb;
    uint8_t       *bitmap;
    uint8_t       *bitmap_dirty;            /* one flag per bitmap block */
    m5fs_dirent_t *dir;
    uint8_t        dir_dirty[M5FS_DIR_BLOCKS];
    uint32_t       free_blocks;
    fd_t           fds[FD_MAX];
} V;

static mutex_t s_lock = MUTEX_INIT;
static uint8_t s_bounce[M5FS_BLOCK_SIZE] __attribute__((aligned(16)));   /* guarded by s_lock */

/* ---------------------------------------------------------------- helpers */

static bool str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void mem_copy(void *d, const void *s, size_t n) { uint8_t *dd = d; const uint8_t *ss = s; while (n--) *dd++ = *ss++; }
static void mem_zero(void *d, size_t n)                 { uint8_t *dd = d; while (n--) *dd++ = 0; }
static uint32_t min_u32(uint32_t a, uint32_t b)          { return a < b ? a : b; }

static int dev_read(uint32_t rel_lba, uint32_t n, void *buf)
{
    return V.bd->read(V.bd, V.base + rel_lba, n, buf);
}
static int dev_write(uint32_t rel_lba, uint32_t n, const void *buf)
{
    return V.bd->write(V.bd, V.base + rel_lba, n, buf);
}

static bool bit_get(uint32_t b) { return (V.bitmap[b >> 3] >> (b & 7u)) & 1u; }
static void bit_set(uint32_t b, bool used)
{
    if (used) { V.bitmap[b >> 3] |= (uint8_t)(1u << (b & 7u)); V.free_blocks--; }
    else      { V.bitmap[b >> 3] &= (uint8_t)~(1u << (b & 7u)); V.free_blocks++; }
    V.bitmap_dirty[b / BITS_PER_BLOCK] = 1;
}

static void dir_mark(int idx) { V.dir_dirty[(uint32_t)idx * M5FS_DIRENT_SIZE / M5FS_BLOCK_SIZE] = 1; }

/* First-fit run of >= want free data blocks; if none, the largest run.
 * Returns the run length actually found (0 = disk full) and its start. */
static uint32_t find_run(uint32_t want, uint32_t *start)
{
    uint32_t best_len = 0, best_start = 0, i = V.sb.data_start;
    while (i < V.sb.total_blocks) {
        if ((i & 7u) == 0 && V.bitmap[i >> 3] == 0xFF) { i += 8; continue; }
        if (bit_get(i)) { i++; continue; }
        uint32_t run_start = i;
        while (i < V.sb.total_blocks && !bit_get(i)) i++;
        uint32_t len = i - run_start;
        if (len >= want) { *start = run_start; return want; }
        if (len > best_len) { best_len = len; best_start = run_start; }
    }
    *start = best_start;
    return best_len;
}

static uint32_t file_blocks(const m5fs_dirent_t *e)
{
    uint32_t n = 0;
    for (int i = 0; i < M5FS_EXTENTS; i++) n += e->extent[i].count;
    return n;
}

/* File block index -> volume LBA, and how many blocks follow contiguously. */
static bool map_block(const m5fs_dirent_t *e, uint32_t fb, uint32_t *lba, uint32_t *run)
{
    for (int i = 0; i < M5FS_EXTENTS && e->extent[i].count; i++) {
        if (fb < e->extent[i].count) {
            *lba = e->extent[i].start + fb;
            *run = e->extent[i].count - fb;
            return true;
        }
        fb -= e->extent[i].count;
    }
    return false;
}

/* Give the file `add` more blocks: extend the last extent in place if the
 * blocks after it are free, then add new extents.  May stop early (ERR_NOSPC)
 * leaving what was allocated attached to the file. */
static int grow(m5fs_dirent_t *e, uint32_t add)
{
    int n = 0;
    while (n < M5FS_EXTENTS && e->extent[n].count) n++;
    if (n > 0) {
        m5fs_extent_t *x = &e->extent[n - 1];
        while (add && x->start + x->count < V.sb.total_blocks && !bit_get(x->start + x->count)) {
            bit_set(x->start + x->count, true);
            x->count++;
            add--;
        }
    }
    while (add) {
        if (n >= M5FS_EXTENTS) return ERR_NOSPC;
        uint32_t start, got = find_run(add, &start);
        if (!got) return ERR_NOSPC;
        for (uint32_t b = 0; b < got; b++) bit_set(start + b, true);
        e->extent[n].start = start;
        e->extent[n].count = got;
        n++;
        add -= got;
    }
    return ERR_OK;
}

static void free_file_blocks(m5fs_dirent_t *e)
{
    for (int i = 0; i < M5FS_EXTENTS; i++) {
        for (uint32_t b = 0; b < e->extent[i].count; b++) bit_set(e->extent[i].start + b, false);
        e->extent[i].start = e->extent[i].count = 0;
    }
}

/* ------------------------------------------------------------ path lookup */

static int find_child(uint8_t parent, const char *name)
{
    for (uint32_t i = 0; i < M5FS_DIR_ENTRIES; i++) {
        const m5fs_dirent_t *e = &V.dir[i];
        if (e->type != M5FS_T_FREE && e->parent == parent && str_eq(e->name, name)) return (int)i;
    }
    return IDX_NONE;
}

/* Resolve every component but the last (which must be directories).
 * On success: *parent = directory containing the last component (or M5FS_ROOT),
 * leaf = its name, *idx = its entry, IDX_NONE if absent, IDX_ROOT for "/". */
static int lookup(const char *path, uint8_t *parent, char *leaf, int *idx)
{
    if (!path || path[0] != '/') return ERR_INVAL;
    uint8_t cur = M5FS_ROOT;
    *parent = M5FS_ROOT;
    leaf[0] = 0;
    *idx = IDX_ROOT;

    const char *p = path;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char comp[FS_NAME_MAX + 1];
        int n = 0;
        while (*p && *p != '/') {
            if (n >= FS_NAME_MAX) return ERR_NAMETOOLONG;
            comp[n++] = *p++;
        }
        comp[n] = 0;
        while (*p == '/') p++;

        int child = find_child(cur, comp);
        if (*p == 0) {                                   /* last component */
            *parent = cur;
            mem_copy(leaf, comp, (size_t)n + 1);
            *idx = child;
            return ERR_OK;
        }
        if (child < 0) return ERR_NOENT;
        if (V.dir[child].type != M5FS_T_DIR) return ERR_NOTDIR;
        cur = (uint8_t)child;
    }
    return ERR_OK;                                       /* "/" */
}

static int new_entry(uint8_t type, uint8_t parent, const char *name)
{
    for (uint32_t i = 0; i < M5FS_DIR_ENTRIES; i++) {
        if (V.dir[i].type == M5FS_T_FREE) {
            m5fs_dirent_t *e = &V.dir[i];
            mem_zero(e, sizeof *e);
            int k = 0;
            while (name[k] && k < M5FS_NAME_LEN - 1) { e->name[k] = name[k]; k++; }
            e->type = type;
            e->parent = parent;
            dir_mark((int)i);
            return (int)i;
        }
    }
    return ERR_NOSPC;
}

/* ------------------------------------------------------- metadata flushing */

static int flush_meta(void)
{
    int rc = ERR_OK;
    uint32_t bm_blocks = V.sb.bitmap_blocks;
    for (uint32_t b = 0; b < bm_blocks && rc == ERR_OK; b++) {
        if (V.bitmap_dirty[b]) {
            rc = dev_write(V.sb.bitmap_start + b, 1, V.bitmap + b * M5FS_BLOCK_SIZE);
            if (rc == ERR_OK) V.bitmap_dirty[b] = 0;
        }
    }
    for (uint32_t b = 0; b < M5FS_DIR_BLOCKS && rc == ERR_OK; b++) {
        if (V.dir_dirty[b]) {
            rc = dev_write(V.sb.dir_start + b, 1, (uint8_t *)V.dir + b * M5FS_BLOCK_SIZE);
            if (rc == ERR_OK) V.dir_dirty[b] = 0;
        }
    }
    return rc;
}

static void sb_seal(m5fs_super_t *sb)
{
    sb->crc32 = crc32(sb, __builtin_offsetof(m5fs_super_t, crc32));
}

static int write_super(void)
{
    uint8_t blk[M5FS_BLOCK_SIZE];
    mem_zero(blk, sizeof blk);
    V.sb.free_blocks = V.free_blocks;
    sb_seal(&V.sb);
    mem_copy(blk, &V.sb, sizeof V.sb);
    return dev_write(0, 1, blk);
}

/* ------------------------------------------------------------ format/mount */

int fs_format(blockdev_t *bd, uint32_t base_lba, uint32_t nblocks)
{
    if (!bd || bd->block_size != M5FS_BLOCK_SIZE || nblocks < M5FS_MIN_BLOCKS ||
        base_lba + nblocks > bd->block_count) return ERR_INVAL;

    mutex_lock(&s_lock);
    int rc = ERR_OK;
    m5fs_super_t sb = { .version = M5FS_VERSION, .block_size = M5FS_BLOCK_SIZE, .total_blocks = nblocks };
    mem_copy(sb.magic, M5FS_MAGIC, 4);
    sb.bitmap_start  = 1;
    sb.bitmap_blocks = (nblocks + BITS_PER_BLOCK - 1) / BITS_PER_BLOCK;
    sb.dir_start     = sb.bitmap_start + sb.bitmap_blocks;
    sb.dir_blocks    = M5FS_DIR_BLOCKS;
    sb.data_start    = sb.dir_start + sb.dir_blocks;
    sb.dir_entries   = M5FS_DIR_ENTRIES;
    if (sb.data_start >= nblocks) { mutex_unlock(&s_lock); return ERR_INVAL; }

    uint8_t *tmp = kmalloc(M5FS_BLOCK_SIZE * M5FS_DIR_BLOCKS);
    if (!tmp) { mutex_unlock(&s_lock); return ERR_NOMEM; }

    /* 1. invalidate any previous volume first (so a power cut mid-format leaves nothing mountable) */
    mem_zero(tmp, M5FS_BLOCK_SIZE);
    rc = bd->write(bd, base_lba, 1, tmp);

    /* 2. bitmap: metadata blocks and the unused tail of the last bitmap block are "in use" */
    for (uint32_t b = 0; b < sb.bitmap_blocks && rc == ERR_OK; b++) {
        mem_zero(tmp, M5FS_BLOCK_SIZE);
        for (uint32_t bit = 0; bit < BITS_PER_BLOCK; bit++) {
            uint32_t blk = b * BITS_PER_BLOCK + bit;
            if (blk < sb.data_start || blk >= nblocks) tmp[bit >> 3] |= (uint8_t)(1u << (bit & 7u));
        }
        rc = bd->write(bd, base_lba + sb.bitmap_start + b, 1, tmp);
    }

    /* 3. directory table with the standard top-level directories */
    if (rc == ERR_OK) {
        mem_zero(tmp, M5FS_BLOCK_SIZE * M5FS_DIR_BLOCKS);
        m5fs_dirent_t *d = (m5fs_dirent_t *)tmp;
        static const char *const dirs[] = { "system", "apps", "home" };
        for (int i = 0; i < 3; i++) {
            int k = 0;
            while (dirs[i][k]) { d[i].name[k] = dirs[i][k]; k++; }
            d[i].type = M5FS_T_DIR;
            d[i].parent = M5FS_ROOT;
        }
        rc = bd->write(bd, base_lba + sb.dir_start, M5FS_DIR_BLOCKS, tmp);
    }

    /* 4. superblock LAST */
    if (rc == ERR_OK) {
        sb.free_blocks = nblocks - sb.data_start;
        sb_seal(&sb);
        mem_zero(tmp, M5FS_BLOCK_SIZE);
        mem_copy(tmp, &sb, sizeof sb);
        rc = bd->write(bd, base_lba, 1, tmp);
    }
    kfree(tmp);
    mutex_unlock(&s_lock);
    return rc;
}

static void release_buffers(void)
{
    kfree(V.bitmap); kfree(V.bitmap_dirty); kfree(V.dir);
    V.bitmap = NULL; V.bitmap_dirty = NULL; V.dir = NULL;
}

int fs_mount(blockdev_t *bd, uint32_t base_lba)
{
    if (!bd || bd->block_size != M5FS_BLOCK_SIZE || base_lba >= bd->block_count) return ERR_INVAL;
    mutex_lock(&s_lock);
    if (V.mounted) { mutex_unlock(&s_lock); return ERR_BUSY; }

    uint8_t blk[M5FS_BLOCK_SIZE];
    int rc = bd->read(bd, base_lba, 1, blk);
    if (rc != ERR_OK) { mutex_unlock(&s_lock); return rc; }

    m5fs_super_t sb;
    mem_copy(&sb, blk, sizeof sb);
    uint32_t bm_blocks = (sb.total_blocks + BITS_PER_BLOCK - 1) / BITS_PER_BLOCK;
    bool good = sb.magic[0] == 'M' && sb.magic[1] == '5' && sb.magic[2] == 'F' && sb.magic[3] == 'S' &&
                sb.version == M5FS_VERSION && sb.block_size == M5FS_BLOCK_SIZE &&
                sb.crc32 == crc32(&sb, __builtin_offsetof(m5fs_super_t, crc32)) &&
                sb.total_blocks >= M5FS_MIN_BLOCKS && base_lba + sb.total_blocks <= bd->block_count &&
                sb.bitmap_start == 1 && sb.bitmap_blocks == bm_blocks &&
                sb.dir_start == 1 + bm_blocks && sb.dir_blocks == M5FS_DIR_BLOCKS &&
                sb.data_start == sb.dir_start + sb.dir_blocks && sb.dir_entries == M5FS_DIR_ENTRIES &&
                sb.data_start < sb.total_blocks;
    if (!good) { mutex_unlock(&s_lock); return ERR_CORRUPT; }

    V.bd = bd; V.base = base_lba; V.sb = sb;
    V.bitmap = kmalloc_psram(bm_blocks * M5FS_BLOCK_SIZE);
    if (!V.bitmap) V.bitmap = kmalloc(bm_blocks * M5FS_BLOCK_SIZE);
    V.bitmap_dirty = kcalloc(bm_blocks, 1);
    V.dir = kmalloc(M5FS_DIR_BLOCKS * M5FS_BLOCK_SIZE);
    if (!V.bitmap || !V.bitmap_dirty || !V.dir) { release_buffers(); mutex_unlock(&s_lock); return ERR_NOMEM; }

    rc = dev_read(sb.bitmap_start, bm_blocks, V.bitmap);
    if (rc == ERR_OK) rc = dev_read(sb.dir_start, M5FS_DIR_BLOCKS, V.dir);
    if (rc != ERR_OK) { release_buffers(); mutex_unlock(&s_lock); return rc; }

    /* sanity-check the directory so later code can trust extents and parents */
    for (uint32_t i = 0; i < M5FS_DIR_ENTRIES && good; i++) {
        const m5fs_dirent_t *e = &V.dir[i];
        if (e->type == M5FS_T_FREE) continue;
        if (e->type != M5FS_T_FILE && e->type != M5FS_T_DIR) good = false;
        if (e->parent != M5FS_ROOT && (e->parent >= M5FS_DIR_ENTRIES || V.dir[e->parent].type != M5FS_T_DIR)) good = false;
        for (int x = 0; x < M5FS_EXTENTS; x++) {
            if (e->extent[x].count &&
                (e->extent[x].start < sb.data_start || e->extent[x].start + e->extent[x].count > sb.total_blocks)) good = false;
        }
        if (e->type == M5FS_T_FILE && (uint64_t)e->size > (uint64_t)file_blocks(e) * M5FS_BLOCK_SIZE) good = false;
    }
    if (!good) { release_buffers(); mutex_unlock(&s_lock); return ERR_CORRUPT; }

    V.free_blocks = 0;                                   /* the bitmap is authoritative */
    for (uint32_t b = 0; b < sb.total_blocks; b++) if (!bit_get(b)) V.free_blocks++;
    for (int i = 0; i < FD_MAX; i++) V.fds[i].used = false;
    V.mounted = true;
    mutex_unlock(&s_lock);
    return ERR_OK;
}

static int sync_locked(void)
{
    int rc = flush_meta();
    if (rc == ERR_OK) rc = write_super();
    return rc;
}

int fs_sync(void)
{
    mutex_lock(&s_lock);
    int rc = V.mounted ? sync_locked() : ERR_INVAL;
    mutex_unlock(&s_lock);
    return rc;
}

int fs_unmount(void)
{
    mutex_lock(&s_lock);
    if (!V.mounted) { mutex_unlock(&s_lock); return ERR_INVAL; }
    for (int i = 0; i < FD_MAX; i++) if (V.fds[i].used) { mutex_unlock(&s_lock); return ERR_BUSY; }
    int rc = sync_locked();
    release_buffers();
    V.mounted = false;
    mutex_unlock(&s_lock);
    return rc;
}

int fs_statfs(fs_stat_t *out)
{
    mutex_lock(&s_lock);
    int rc = ERR_INVAL;
    if (V.mounted && out) {
        out->block_size = M5FS_BLOCK_SIZE; out->total_blocks = V.sb.total_blocks; out->free_blocks = V.free_blocks;
        rc = ERR_OK;
    }
    mutex_unlock(&s_lock);
    return rc;
}

/* ------------------------------------------------------------------- files */

static int fd_check(int fd, uint32_t need_flag)
{
    if (!V.mounted) return ERR_INVAL;
    if (fd < 0 || fd >= FD_MAX || !V.fds[fd].used) return ERR_BADF;
    if (need_flag && !(V.fds[fd].flags & need_flag)) return ERR_INVAL;
    return ERR_OK;
}

int fs_open(const char *path, uint32_t flags)
{
    if (!(flags & (FS_O_READ | FS_O_WRITE))) return ERR_INVAL;
    mutex_lock(&s_lock);
    int result;
    uint8_t parent; char leaf[FS_NAME_MAX + 1]; int idx;
    if (!V.mounted) { result = ERR_INVAL; goto out; }
    result = lookup(path, &parent, leaf, &idx);
    if (result != ERR_OK) goto out;
    if (idx == IDX_ROOT) { result = ERR_ISDIR; goto out; }

    if (idx == IDX_NONE) {
        if (!(flags & FS_O_CREATE) || !(flags & FS_O_WRITE)) { result = ERR_NOENT; goto out; }
        idx = new_entry(M5FS_T_FILE, parent, leaf);
        if (idx < 0) { result = idx; goto out; }
    } else if (V.dir[idx].type == M5FS_T_DIR) {
        result = ERR_ISDIR; goto out;
    } else if ((flags & FS_O_TRUNC) && (flags & FS_O_WRITE)) {
        free_file_blocks(&V.dir[idx]);
        V.dir[idx].size = 0;
        dir_mark(idx);
    }

    int fd = -1;
    for (int i = 0; i < FD_MAX; i++) if (!V.fds[i].used) { fd = i; break; }
    if (fd < 0) { result = ERR_BUSY; goto out; }
    V.fds[fd] = (fd_t){ .used = true, .ent = (uint8_t)idx, .pos = 0, .flags = flags };
    result = fd;
out:
    mutex_unlock(&s_lock);
    return result;
}

int fs_close(int fd)
{
    mutex_lock(&s_lock);
    int rc = fd_check(fd, 0);
    if (rc == ERR_OK) {
        V.fds[fd].used = false;
        rc = flush_meta();
    }
    mutex_unlock(&s_lock);
    return rc;
}

int fs_read(int fd, void *buf, size_t len)
{
    mutex_lock(&s_lock);
    int rc = fd_check(fd, FS_O_READ);
    if (rc != ERR_OK) { mutex_unlock(&s_lock); return rc; }

    fd_t *f = &V.fds[fd];
    const m5fs_dirent_t *e = &V.dir[f->ent];
    uint8_t *out = buf;
    uint32_t n = f->pos >= e->size ? 0 : min_u32((uint32_t)len, e->size - f->pos);
    uint32_t done = 0;
    while (done < n && rc == ERR_OK) {
        uint32_t lba, run;
        if (!map_block(e, f->pos / M5FS_BLOCK_SIZE, &lba, &run)) { rc = ERR_CORRUPT; break; }
        uint32_t off = f->pos % M5FS_BLOCK_SIZE;
        if (off == 0 && n - done >= M5FS_BLOCK_SIZE) {              /* whole blocks: straight into the caller's buffer */
            uint32_t blocks = min_u32((n - done) / M5FS_BLOCK_SIZE, run);
            rc = dev_read(lba, blocks, out + done);
            done += blocks * M5FS_BLOCK_SIZE; f->pos += blocks * M5FS_BLOCK_SIZE;
        } else {                                                    /* partial block: via the bounce buffer */
            rc = dev_read(lba, 1, s_bounce);
            uint32_t chunk = min_u32(M5FS_BLOCK_SIZE - off, n - done);
            if (rc == ERR_OK) mem_copy(out + done, s_bounce + off, chunk);
            done += chunk; f->pos += chunk;
        }
    }
    mutex_unlock(&s_lock);
    return rc == ERR_OK ? (int)done : rc;
}

int fs_write(int fd, const void *buf, size_t len)
{
    mutex_lock(&s_lock);
    int rc = fd_check(fd, FS_O_WRITE);
    if (rc != ERR_OK) { mutex_unlock(&s_lock); return rc; }

    fd_t *f = &V.fds[fd];
    m5fs_dirent_t *e = &V.dir[f->ent];
    const uint8_t *in = buf;
    if (f->flags & FS_O_APPEND) f->pos = e->size;
    if (len == 0) { mutex_unlock(&s_lock); return 0; }
    if ((uint64_t)f->pos + len > 0x7FFFFFFFu) { mutex_unlock(&s_lock); return ERR_INVAL; }

    uint32_t end = f->pos + (uint32_t)len;
    uint32_t need = (end + M5FS_BLOCK_SIZE - 1) / M5FS_BLOCK_SIZE, have = file_blocks(e);
    if (need > have) { grow(e, need - have); dir_mark((int)(e - V.dir)); }   /* may fall short: we write what fits */
    uint32_t cap = file_blocks(e) * M5FS_BLOCK_SIZE;
    uint32_t n = f->pos >= cap ? 0 : min_u32((uint32_t)len, cap - f->pos);
    if (n == 0) { mutex_unlock(&s_lock); return ERR_NOSPC; }

    uint32_t done = 0;
    while (done < n && rc == ERR_OK) {
        uint32_t lba, run;
        if (!map_block(e, f->pos / M5FS_BLOCK_SIZE, &lba, &run)) { rc = ERR_CORRUPT; break; }
        uint32_t off = f->pos % M5FS_BLOCK_SIZE;
        if (off == 0 && n - done >= M5FS_BLOCK_SIZE) {
            uint32_t blocks = min_u32((n - done) / M5FS_BLOCK_SIZE, run);
            rc = dev_write(lba, blocks, in + done);
            done += blocks * M5FS_BLOCK_SIZE; f->pos += blocks * M5FS_BLOCK_SIZE;
        } else {                                                    /* read-modify-write one block */
            uint32_t blk_start = (f->pos / M5FS_BLOCK_SIZE) * M5FS_BLOCK_SIZE;
            if (blk_start >= e->size) mem_zero(s_bounce, sizeof s_bounce);     /* fresh block: nothing to preserve */
            else rc = dev_read(lba, 1, s_bounce);
            uint32_t chunk = min_u32(M5FS_BLOCK_SIZE - off, n - done);
            if (rc == ERR_OK) { mem_copy(s_bounce + off, in + done, chunk); rc = dev_write(lba, 1, s_bounce); }
            done += chunk; f->pos += chunk;
        }
        if (f->pos > e->size) { e->size = f->pos; dir_mark((int)(e - V.dir)); }
    }
    mutex_unlock(&s_lock);
    return rc == ERR_OK ? (int)done : rc;
}

int fs_seek(int fd, int32_t offset, int whence)
{
    mutex_lock(&s_lock);
    int rc = fd_check(fd, 0);
    if (rc == ERR_OK) {
        fd_t *f = &V.fds[fd];
        int64_t base = whence == FS_SEEK_SET ? 0 : whence == FS_SEEK_CUR ? (int64_t)f->pos : (int64_t)V.dir[f->ent].size;
        int64_t pos = base + offset;
        if ((whence != FS_SEEK_SET && whence != FS_SEEK_CUR && whence != FS_SEEK_END) || pos < 0 || pos > (int64_t)V.dir[f->ent].size) {
            rc = ERR_INVAL;                 /* no sparse files: cannot seek past the end */
        } else {
            f->pos = (uint32_t)pos;
            rc = (int)f->pos;
        }
    }
    mutex_unlock(&s_lock);
    return rc;
}

/* ------------------------------------------------- directories & metadata */

static void fill_info(fs_info_t *info, const m5fs_dirent_t *e)
{
    int k = 0;
    while (e->name[k] && k < FS_NAME_MAX) { info->name[k] = e->name[k]; k++; }
    info->name[k] = 0;
    info->type = e->type == M5FS_T_DIR ? FS_TYPE_DIR : FS_TYPE_FILE;
    info->size = e->type == M5FS_T_DIR ? 0 : e->size;
}

int fs_stat(const char *path, fs_info_t *out)
{
    mutex_lock(&s_lock);
    int rc = V.mounted ? ERR_OK : ERR_INVAL;
    uint8_t parent; char leaf[FS_NAME_MAX + 1]; int idx;
    if (rc == ERR_OK) rc = lookup(path, &parent, leaf, &idx);
    if (rc == ERR_OK) {
        if (idx == IDX_ROOT) { out->name[0] = '/'; out->name[1] = 0; out->type = FS_TYPE_DIR; out->size = 0; }
        else if (idx == IDX_NONE) rc = ERR_NOENT;
        else fill_info(out, &V.dir[idx]);
    }
    mutex_unlock(&s_lock);
    return rc;
}

int fs_mkdir(const char *path)
{
    mutex_lock(&s_lock);
    int rc = V.mounted ? ERR_OK : ERR_INVAL;
    uint8_t parent; char leaf[FS_NAME_MAX + 1]; int idx;
    if (rc == ERR_OK) rc = lookup(path, &parent, leaf, &idx);
    if (rc == ERR_OK) {
        if (idx != IDX_NONE) rc = ERR_EXIST;
        else {
            int e = new_entry(M5FS_T_DIR, parent, leaf);
            rc = e < 0 ? e : flush_meta();
        }
    }
    mutex_unlock(&s_lock);
    return rc;
}

int fs_delete(const char *path)
{
    mutex_lock(&s_lock);
    int rc = V.mounted ? ERR_OK : ERR_INVAL;
    uint8_t parent; char leaf[FS_NAME_MAX + 1]; int idx;
    if (rc == ERR_OK) rc = lookup(path, &parent, leaf, &idx);
    if (rc == ERR_OK) {
        if (idx == IDX_ROOT) rc = ERR_INVAL;
        else if (idx == IDX_NONE) rc = ERR_NOENT;
    }
    if (rc == ERR_OK) {
        for (int i = 0; i < FD_MAX && rc == ERR_OK; i++) if (V.fds[i].used && V.fds[i].ent == idx) rc = ERR_BUSY;
        if (rc == ERR_OK && V.dir[idx].type == M5FS_T_DIR) {
            for (uint32_t i = 0; i < M5FS_DIR_ENTRIES; i++)
                if (V.dir[i].type != M5FS_T_FREE && V.dir[i].parent == idx) { rc = ERR_NOTEMPTY; break; }
        }
    }
    if (rc == ERR_OK) {
        free_file_blocks(&V.dir[idx]);
        mem_zero(&V.dir[idx], sizeof V.dir[idx]);
        dir_mark(idx);
        rc = flush_meta();
    }
    mutex_unlock(&s_lock);
    return rc;
}

int fs_list(const char *path, int (*cb)(const fs_info_t *, void *), void *ctx)
{
    mutex_lock(&s_lock);               /* cb runs under the lock: it must not call fs_*() */
    int rc = V.mounted ? ERR_OK : ERR_INVAL;
    uint8_t parent; char leaf[FS_NAME_MAX + 1]; int idx;
    if (rc == ERR_OK) rc = lookup(path, &parent, leaf, &idx);
    if (rc == ERR_OK) {
        if (idx == IDX_NONE) rc = ERR_NOENT;
        else if (idx != IDX_ROOT && V.dir[idx].type != M5FS_T_DIR) rc = ERR_NOTDIR;
    }
    if (rc == ERR_OK) {
        uint8_t dir_id = idx == IDX_ROOT ? M5FS_ROOT : (uint8_t)idx;
        int count = 0;
        for (uint32_t i = 0; i < M5FS_DIR_ENTRIES; i++) {
            const m5fs_dirent_t *e = &V.dir[i];
            if (e->type == M5FS_T_FREE || e->parent != dir_id) continue;
            fs_info_t info;
            fill_info(&info, e);
            count++;
            if (cb(&info, ctx)) break;
        }
        rc = count;
    }
    mutex_unlock(&s_lock);
    return rc;
}
