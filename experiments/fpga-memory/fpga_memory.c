#include "fpga_memory.h"

enum fm_status fm_init(struct fm_region *r, void *backend, uint64_t base,
                       uint64_t length, size_t chunk, fm_read_fn read,
                       fm_write_fn write)
{
    if (!r || !chunk || (!read && !write)) return FM_INVALID;
    if (length > UINT64_MAX - base) return FM_RANGE;
    *r = (struct fm_region){backend, base, length, chunk, read, write};
    return FM_OK;
}

static struct fm_result transfer(const struct fm_region *r, uint64_t offset,
                                void *dst, const void *src, size_t n, int writing)
{
    struct fm_result out = {FM_OK, 0, 0};
    if (!r || !r->max_chunk || (writing ? !r->write : !r->read) ||
        (n && (writing ? !src : !dst))) {
        out.status = FM_INVALID;
        return out;
    }
    /* Check all bounds before calling backend; subtraction cannot wrap. */
    if (r->length > UINT64_MAX - r->base || offset > r->length ||
        n > r->length - offset) {
        out.status = FM_RANGE;
        return out;
    }
    while (out.completed < n) {
        size_t count = n - out.completed;
        if (count > r->max_chunk) count = r->max_chunk;
        uint64_t address = r->base + offset + out.completed;
        int rc;
        ++out.calls;
        if (writing)
            rc = r->write(r->backend, address,
                          (const unsigned char *)src + out.completed, count);
        else
            rc = r->read(r->backend, address,
                         (unsigned char *)dst + out.completed, count);
        if (rc) {
            out.status = FM_BACKEND;
            break;
        }
        out.completed += count;
    }
    return out;
}

struct fm_result fm_read(const struct fm_region *r, uint64_t off, void *dst, size_t n)
{
    return transfer(r, off, dst, NULL, n, 0);
}
struct fm_result fm_write(const struct fm_region *r, uint64_t off, const void *src, size_t n)
{
    return transfer(r, off, NULL, src, n, 1);
}
