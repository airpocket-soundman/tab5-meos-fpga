#pragma once
#include <m5os/blockdev.h>
/* RAM-backed block device in PSRAM (falls back to internal RAM). For tests and scratch volumes. */
blockdev_t *ramdisk_create(uint32_t blocks);
void        ramdisk_destroy(blockdev_t *bd);
