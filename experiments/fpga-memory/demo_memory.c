#include "fpga_memory.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SIZE (64u * 1024u * 1024u)
#define TILE_SIZE 65536u
struct file_backend { FILE *file; uint64_t bytes; };
static uint32_t crc_update(uint32_t crc, const unsigned char *data, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}
static int seek_region(struct file_backend *b, uint64_t address, size_t n)
{
    if (address > b->bytes || n > b->bytes - address) return -1;
    /* Total file extent <=128MiB, within even 32-bit signed long. */
    return fseek(b->file, (long)address, SEEK_SET);
}
static int file_read(void *ctx, uint64_t address, void *dst, size_t n)
{
    struct file_backend *b = ctx;
    if (seek_region(b, address, n)) return -1;
    return fread(dst, 1, n, b->file) == n ? 0 : -1;
}
static int file_write(void *ctx, uint64_t address, const void *src, size_t n)
{
    struct file_backend *b = ctx;
    if (seek_region(b, address, n)) return -1;
    return fwrite(src, 1, n, b->file) == n ? 0 : -1;
}
static int number(const char *s, uint64_t limit, uint64_t *out)
{
    uint64_t n = 0;
    if (!*s) return 0;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return 0;
        unsigned digit = (unsigned)(*s - '0');
        if (n > limit / 10 || (n == limit / 10 && digit > limit % 10)) return 0;
        n = n * 10 + digit;
    }
    *out = n;
    return 1;
}
int main(int argc, char **argv)
{
    uint64_t size = 1024 * 1024 + 17, limit = 4096;
    int mock = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--mock")) mock = 1;
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) {
            if (!number(argv[++i], MAX_SIZE, &size)) return 2;
        } else if (!strcmp(argv[i], "--transfer-limit") && i + 1 < argc) {
            if (!number(argv[++i], TILE_SIZE, &limit) || !limit) return 2;
        } else return 2;
    }
    if (!mock) { fputs("--mock required: host file backend only\n", stderr); return 2; }
    if ((crc_update(UINT32_MAX, (const unsigned char *)"123456789", 9) ^ UINT32_MAX)
        != 0xcbf43926u) { fputs("CRC32 self-test failed\n", stderr); return 1; }
    struct file_backend backend = {tmpfile(), size * 2};
    if (!backend.file) { perror("tmpfile"); return 1; }
    struct fm_region source, restored;
    if (fm_init(&source, &backend, 0, size, (size_t)limit, file_read, file_write) != FM_OK ||
        fm_init(&restored, &backend, size, size, (size_t)limit, file_read, file_write) != FM_OK) {
        fclose(backend.file); return 1;
    }
    unsigned char tile[TILE_SIZE];
    uint32_t expected = UINT32_MAX, actual = UINT32_MAX;
    size_t read_calls = 0, write_calls = 0;
    int failed = 0;
    for (uint64_t offset = 0; offset < size;) {
        size_t count = (size_t)(size - offset);
        if (count > TILE_SIZE) count = TILE_SIZE;
        for (size_t i = 0; i < count; ++i) tile[i] = (unsigned char)((offset + i) % 251);
        expected = crc_update(expected, tile, count);
        struct fm_result a = fm_write(&source, offset, tile, count);
        write_calls += a.calls;
        if (a.status != FM_OK) { failed = 1; break; }
        offset += count;
    }
    for (uint64_t offset = 0; !failed && offset < size;) {
        size_t count = (size_t)(size - offset);
        if (count > TILE_SIZE) count = TILE_SIZE;
        struct fm_result a = fm_read(&source, offset, tile, count);
        read_calls += a.calls;
        if (a.status != FM_OK) { failed = 1; break; }
        a = fm_write(&restored, offset, tile, count);
        write_calls += a.calls;
        if (a.status != FM_OK) { failed = 1; break; }
        offset += count;
    }
    for (uint64_t offset = 0; !failed && offset < size;) {
        size_t count = (size_t)(size - offset);
        if (count > TILE_SIZE) count = TILE_SIZE;
        struct fm_result a = fm_read(&restored, offset, tile, count);
        read_calls += a.calls;
        if (a.status != FM_OK) { failed = 1; break; }
        for (size_t i = 0; i < count; ++i)
            if (tile[i] != (unsigned char)((offset + i) % 251)) failed = 1;
        actual = crc_update(actual, tile, count);
        offset += count;
    }
    if (fclose(backend.file)) failed = 1;
    if (failed || expected != actual) { fputs("mock transfer verification failed\n", stderr); return 1; }
    printf("{\"backend\":\"host-file-mock\",\"physical_link_tested\":false,"
           "\"bytes_verified\":%" PRIu64 ",\"transfer_limit\":%" PRIu64 ","
           "\"readback_crc32\":\"%08" PRIx32 "\",\"working_buffer_bytes\":%u,"
           "\"read_transfers\":%zu,\"write_transfers\":%zu}\n",
           size, limit, actual ^ UINT32_MAX, TILE_SIZE, read_calls, write_calls);
    return 0;
}
