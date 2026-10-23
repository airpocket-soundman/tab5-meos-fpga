#include <m5os/camera.h>
#include <m5os/errno.h>
#include <m5os/vfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BMP_HDR_SIZE 70u
#define WRITE_CHUNK_BYTES 32768u
#define RGB565(r, g, b) ((((r) & 248) << 8) | (((g) & 252) << 3) | ((b) >> 3))
static uint8_t file[65536];
static size_t file_len, pos;
static bool close_fail, unlinked;
int vfs_open(const char *p, uint32_t f) {
    (void)p;
    pos = 0;
    if (f & VFS_O_TRUNC)
        file_len = 0;
    return 1;
}
int vfs_read(int fd, void *b, size_t n) {
    if (n > 7)
        n = 7;
    if (n > file_len - pos)
        n = file_len - pos;
    memcpy(b, file + pos, n);
    pos += n;
    return n;
}
int vfs_write(int fd, const void *b, size_t n) {
    if (n > 5)
        n = 5;
    if (n > sizeof(file) - pos)
        return -1;
    memcpy(file + pos, b, n);
    pos += n;
    if (pos > file_len)
        file_len = pos;
    return n;
}
int vfs_close(int f) { return close_fail ? -1 : 0; }
int vfs_unlink(const char *p) {
    unlinked = true;
    return 0;
}
int vfs_seek(int f, int32_t off, int whence) {
    if (whence || off < 0 || (size_t)off > file_len)
        return -1;
    return pos = off;
}
void *kmalloc(size_t n) { return n < 10000000 ? malloc(n) : NULL; }
void *kmalloc_psram(size_t n) { return kmalloc(n); }
void kfree(void *p) { free(p); }
#include "../../user/camera/camera_bmp.inc"
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL line %d %s\n", __LINE__, #c);                                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static bool decode_fails(void) {
    uint16_t *p = (void *)1;
    int w = 7, h = 9;
    bool ok = camera_load_bmp("x", &p, &w, &h);
    if (ok)
        free(p);
    return !ok && !p && !w && !h;
}
static void base24(int w, int rawh) {
    memset(file, 0, sizeof(file));
    file[0] = 'B';
    file[1] = 'M';
    put_le32(file + 10, 54);
    put_le32(file + 14, 40);
    put_le32(file + 18, w);
    put_le32(file + 22, rawh);
    put_le16(file + 26, 1);
    put_le16(file + 28, 24);
    file_len = 54 + (((w * 3) + 3) & ~3) * (rawh < 0 ? -rawh : rawh);
}
int main(void) {
    uint16_t original[] = {0xf800, 0x07e0, 0x001f, 0xffff, 0, 0x1234}, *pixels = NULL;
    int w, h;
    CHECK(camera_image_save_rgb565("x", original, 3, 2));
    CHECK(file_len == 86);
    CHECK(get_le32(file + 34) == 16);
    CHECK(file[76] == 0 && file[77] == 0);
    CHECK(camera_load_bmp("x", &pixels, &w, &h));
    CHECK(w == 3 && h == 2 && !memcmp(pixels, original, sizeof(original)));
    free(pixels);
    /* Decoder must honor padded BGR rows and bottom-up order. */
    base24(3, 2);
    file[54 + 2] = 255;
    file[54 + 4] = 255;
    file[54 + 6] = 255;
    file[66] = 255;
    file[66 + 3 + 1] = 255;
    file[66 + 6 + 2] = 255;
    CHECK(camera_load_bmp("x", &pixels, &w, &h));
    CHECK(pixels[0] == 0x001f && pixels[1] == 0x07e0 && pixels[2] == 0xf800 &&
          pixels[3] == 0xf800 && pixels[4] == 0x07e0 && pixels[5] == 0x001f);
    free(pixels);
    /* BI_RGB 16-bit means RGB555, not RGB565. */
    base24(1, -1);
    put_le16(file + 28, 16);
    put_le16(file + 54, 0x03e0);
    file_len = 58;
    CHECK(camera_load_bmp("x", &pixels, &w, &h));
    CHECK(pixels[0] == 0x07e0);
    free(pixels);
    /* Pixel offset can exceed the old 128-byte header buffer. */
    base24(1, -1);
    put_le32(file + 10, 1024);
    file_len = 1028;
    file[1026] = 255;
    CHECK(camera_load_bmp("x", &pixels, &w, &h));
    CHECK(pixels[0] == 0xf800);
    free(pixels);
    base24(1, -1);
    put_le32(file + 22, 0x80000000u);
    CHECK(decode_fails());
    base24(1, -1);
    put_le32(file + 30, 1);
    CHECK(decode_fails());
    base24(1, -1);
    put_le16(file + 26, 2);
    CHECK(decode_fails());
    base24(1, -1);
    put_le32(file + 10, 0xfffffffcu);
    CHECK(decode_fails());
    base24(1, -1);
    put_le32(file + 10, 20);
    CHECK(decode_fails());
    base24(1, -1);
    file_len--;
    CHECK(decode_fails());
    CHECK(camera_image_save_rgb565("x", original, 3, 2));
    put_le32(file + 58, 0x00ff);
    CHECK(decode_fails());
    close_fail = true;
    unlinked = false;
    CHECK(!camera_image_save_rgb565("x", original, 3, 2));
    CHECK(unlinked);
    puts("PASS BMP RGB565 odd-row roundtrip, BGR24 bottom-up, RGB555, metadata offset, malformed "
         "input, short I/O and close failure");
    return 0;
}
