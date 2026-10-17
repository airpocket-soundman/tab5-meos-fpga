#pragma once
#include <m5os/types.h>

/*
 * M5FS on-disk format, version 1.   (All integers little-endian, as on ESP32.)
 *
 *   block 0                 superblock
 *   1 .. bitmap_blocks      allocation bitmap: bit i <-> volume block i
 *                           (1 = in use; metadata blocks are marked in use)
 *   dir_start ..            directory table: 128 fixed entries x 64 bytes
 *   data_start ..           file data
 *
 * Files own up to M5FS_EXTENTS contiguous runs ("extents") of blocks.
 * Directories are entries too; an entry names its parent by index, so a path
 * is resolved by walking names, not by reading directory contents.
 *
 * There is no journal: metadata is written after data, the superblock is
 * written last on format, so a power cut can lose the latest changes but a
 * half-formatted volume is never mountable.
 */
#define M5FS_MAGIC          "M5FS"
#define M5FS_VERSION        1u
#define M5FS_BLOCK_SIZE     512u
#define M5FS_DIR_ENTRIES    128u
#define M5FS_DIRENT_SIZE    64u
#define M5FS_DIR_BLOCKS     ((M5FS_DIR_ENTRIES * M5FS_DIRENT_SIZE) / M5FS_BLOCK_SIZE)
#define M5FS_EXTENTS        4
#define M5FS_NAME_LEN       24            /* including the terminating NUL */
#define M5FS_ROOT           0xFFu         /* parent value meaning "/" */
#define M5FS_MIN_BLOCKS     64u

#define M5FS_T_FREE   0
#define M5FS_T_FILE   1
#define M5FS_T_DIR    2

typedef struct {
    char     magic[4];
    uint32_t version;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t bitmap_start, bitmap_blocks;
    uint32_t dir_start, dir_blocks;
    uint32_t data_start;
    uint32_t dir_entries;
    uint32_t free_blocks;                 /* informational; the bitmap is authoritative */
    uint32_t crc32;                       /* of all bytes before this field */
} m5fs_super_t;

typedef struct { uint32_t start, count; } m5fs_extent_t;

typedef struct {
    char          name[M5FS_NAME_LEN];
    uint8_t       type;                   /* M5FS_T_* */
    uint8_t       parent;                 /* index of parent directory or M5FS_ROOT */
    uint16_t      flags;                  /* reserved, 0 */
    uint32_t      size;                   /* bytes (files) */
    m5fs_extent_t extent[M5FS_EXTENTS];   /* count == 0 terminates the list */
} m5fs_dirent_t;

_Static_assert(sizeof(m5fs_dirent_t) == M5FS_DIRENT_SIZE, "dirent must be 64 bytes");
_Static_assert(sizeof(m5fs_super_t) <= M5FS_BLOCK_SIZE, "superblock must fit one block");
