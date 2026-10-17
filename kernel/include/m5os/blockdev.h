#pragma once
#include <m5os/types.h>

/* A block device: fixed-size blocks addressed by LBA.  Filesystems sit on this,
 * knowing nothing about SD cards or SPI. */
typedef struct blockdev blockdev_t;
struct blockdev {
    const char *name;
    uint32_t    block_size;                 /* bytes, 512 for SD */
    uint32_t    block_count;
    int (*read) (blockdev_t *bd, uint32_t lba, uint32_t count, void *buf);        /* 0 or -ERR */
    int (*write)(blockdev_t *bd, uint32_t lba, uint32_t count, const void *buf);
    void       *priv;
};
