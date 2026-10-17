#include "ramdisk.h"
#include <m5os/kmalloc.h>
#include <m5os/errno.h>

#define RD_BLOCK 512u

typedef struct { blockdev_t bd; uint8_t *mem; } ramdisk_t;

static int rd_read(blockdev_t *bd, uint32_t lba, uint32_t n, void *buf)
{
    ramdisk_t *r = (ramdisk_t *)bd;
    if (lba + n > bd->block_count) return ERR_INVAL;
    uint8_t *dst = buf; const uint8_t *src = r->mem + (size_t)lba * RD_BLOCK;
    for (size_t i = 0; i < (size_t)n * RD_BLOCK; i++) dst[i] = src[i];
    return ERR_OK;
}

static int rd_write(blockdev_t *bd, uint32_t lba, uint32_t n, const void *buf)
{
    ramdisk_t *r = (ramdisk_t *)bd;
    if (lba + n > bd->block_count) return ERR_INVAL;
    const uint8_t *src = buf; uint8_t *dst = r->mem + (size_t)lba * RD_BLOCK;
    for (size_t i = 0; i < (size_t)n * RD_BLOCK; i++) dst[i] = src[i];
    return ERR_OK;
}

blockdev_t *ramdisk_create(uint32_t blocks)
{
    ramdisk_t *r = kmalloc(sizeof *r);
    uint8_t *mem = kmalloc_psram((size_t)blocks * RD_BLOCK);
    if (!mem) mem = kmalloc((size_t)blocks * RD_BLOCK);
    if (!r || !mem) { kfree(r); kfree(mem); return NULL; }
    for (size_t i = 0; i < (size_t)blocks * RD_BLOCK; i++) mem[i] = 0;
    r->mem = mem;
    r->bd = (blockdev_t){ .name = "ram0", .block_size = RD_BLOCK, .block_count = blocks,
                          .read = rd_read, .write = rd_write };
    return &r->bd;
}

void ramdisk_destroy(blockdev_t *bd)
{
    if (!bd) return;
    ramdisk_t *r = (ramdisk_t *)bd;
    kfree(r->mem);
    kfree(r);
}
