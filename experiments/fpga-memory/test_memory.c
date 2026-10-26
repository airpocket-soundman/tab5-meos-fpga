#include "fpga_memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct mock { unsigned char bytes[257]; size_t calls, fail_at; int partial_on_fail; };
static int read_mock(void *ctx, uint64_t address, void *dst, size_t n)
{
    struct mock *m = ctx;
    assert(address >= 4096 && address - 4096 <= sizeof(m->bytes));
    assert(n <= sizeof(m->bytes) - (size_t)(address - 4096));
    if (++m->calls == m->fail_at) {
        if (m->partial_on_fail && n) memcpy(dst, m->bytes + address - 4096, 1);
        return -1;
    }
    memcpy(dst, m->bytes + address - 4096, n);
    return 0;
}
static int write_mock(void *ctx, uint64_t address, const void *src, size_t n)
{
    struct mock *m = ctx;
    assert(address >= 4096 && address - 4096 <= sizeof(m->bytes));
    assert(n <= sizeof(m->bytes) - (size_t)(address - 4096));
    if (++m->calls == m->fail_at) {
        if (m->partial_on_fail && n) memcpy(m->bytes + address - 4096, src, 1);
        return -1;
    }
    memcpy(m->bytes + address - 4096, src, n);
    return 0;
}
int main(void)
{
    struct mock m = {0}; struct fm_region r;
    unsigned char input[257], output[257];
    for (size_t i = 0; i < sizeof(input); ++i) input[i] = (unsigned char)(i * 29);
    assert(fm_init(NULL, &m, 0, 1, 1, read_mock, write_mock) == FM_INVALID);
    assert(fm_init(&r, &m, 0, 1, 0, read_mock, write_mock) == FM_INVALID);
    assert(fm_init(&r, &m, UINT64_MAX, 1, 1, read_mock, write_mock) == FM_RANGE);
    assert(fm_init(&r, &m, 4096, sizeof(input), 31, read_mock, write_mock) == FM_OK);
    struct fm_result a = fm_write(&r, 0, input, sizeof(input));
    assert(a.status == FM_OK && a.completed == 257 && a.calls == 9);
    a = fm_read(&r, 0, output, sizeof(output));
    assert(a.status == FM_OK && a.calls == 9 && !memcmp(input, output, 257));
    size_t before = m.calls;
    assert(fm_read(&r, 257, NULL, 0).status == FM_OK);
    assert(fm_read(&r, 258, NULL, 0).status == FM_RANGE);
    assert(fm_write(&r, 256, input, 2).status == FM_RANGE);
    assert(fm_write(&r, UINT64_MAX, input, 1).status == FM_RANGE);
    assert(fm_read(&r, 0, NULL, 1).status == FM_INVALID);
    assert(m.calls == before);
    m.fail_at = m.calls + 3;
    memset(output, 0xa5, sizeof(output));
    a = fm_read(&r, 0, output, sizeof(output));
    assert(a.status == FM_BACKEND && a.completed == 62 && a.calls == 3);
    assert(!memcmp(input, output, 62) && output[62] == 0xa5);
    m.fail_at = 0;
    a = fm_read(&r, a.completed, output + a.completed, sizeof(output) - a.completed);
    assert(a.status == FM_OK && !memcmp(input, output, sizeof(input)));
    /* A read-only capability must reject writes before any backend operation. */
    r.write = NULL; before = m.calls;
    assert(fm_write(&r, 0, input, 1).status == FM_INVALID && m.calls == before);
    /* Every subregion and awkward chunk size preserves adjacent bytes. */
    for (size_t chunk = 1; chunk <= 33; ++chunk) {
        assert(fm_init(&r, &m, 4096, 257, chunk, read_mock, write_mock) == FM_OK);
        for (size_t offset = 0; offset <= 257; ++offset) {
            size_t length = 257 - offset;
            memset(m.bytes, 0xa5, sizeof(m.bytes));
            a = fm_write(&r, offset, input, length);
            assert(a.status == FM_OK && a.completed == length);
            assert(a.calls == (length + chunk - 1) / chunk);
            assert(!memcmp(m.bytes + offset, input, length));
            for (size_t i = 0; i < offset; ++i) assert(m.bytes[i] == 0xa5);
        }
    }
    assert(fm_init(&r, &m, 4096, 257, 31, read_mock, write_mock) == FM_OK);
    for (size_t fail = 1; fail <= 9; ++fail) {
        memset(m.bytes, 0xa5, sizeof(m.bytes));
        m.fail_at = m.calls + fail;
        a = fm_write(&r, 0, input, 257);
        assert(a.status == FM_BACKEND && a.calls == fail);
        assert(a.completed == (fail - 1) * 31);
        assert(!memcmp(input, m.bytes, a.completed));
        assert(m.bytes[a.completed] == 0xa5);
    }
    /* Failed chunk may partially mutate, but contributes no confirmed progress. */
    memset(m.bytes, 0xa5, sizeof(m.bytes));
    m.partial_on_fail = 1; m.fail_at = m.calls + 2;
    a = fm_write(&r, 0, input, sizeof(input));
    assert(a.status == FM_BACKEND && a.completed == 31 && a.calls == 2);
    assert(!memcmp(m.bytes, input, 32) && m.bytes[32] == 0xa5);
    memset(output, 0xcc, sizeof(output));
    m.fail_at = m.calls + 2;
    a = fm_read(&r, 0, output, sizeof(output));
    assert(a.status == FM_BACKEND && a.completed == 31 && a.calls == 2);
    assert(!memcmp(output, input, 32) && output[32] == 0xcc);
    m.partial_on_fail = 0;
    /* Empty range at maximum address is legal; any nonempty access is rejected. */
    assert(fm_init(&r, &m, UINT64_MAX, 0, 31, read_mock, write_mock) == FM_OK);
    before = m.calls;
    assert(fm_read(&r, 0, NULL, 0).status == FM_OK);
    assert(fm_read(&r, 0, output, 1).status == FM_RANGE && m.calls == before);
    /* Working set stays at 17 bytes even though region is 257 bytes. */
    unsigned char tile[17];
    assert(fm_init(&r, &m, 4096, 257, 7, read_mock, write_mock) == FM_OK);
    m.fail_at = 0;
    for (size_t pos = 0; pos < 257; pos += sizeof(tile)) {
        size_t count = 257 - pos;
        if (count > sizeof(tile)) count = sizeof(tile);
        for (size_t i = 0; i < count; ++i) tile[i] = (unsigned char)((pos + i) * 29);
        assert(fm_write(&r, pos, tile, count).status == FM_OK);
    }
    for (size_t pos = 0; pos < 257; pos += sizeof(tile)) {
        size_t count = 257 - pos;
        if (count > sizeof(tile)) count = sizeof(tile);
        assert(fm_read(&r, pos, tile, count).status == FM_OK);
        for (size_t i = 0; i < count; ++i) assert(tile[i] == (unsigned char)((pos + i) * 29));
    }
    puts("PASS: split transfer, bounds, overflow, capabilities, failure progress, resume");
    return 0;
}
