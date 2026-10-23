static void put_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint16_t get_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool camera_is_bmp_name(const char *name) {
    if (!name)
        return false;
    int n = 0;
    while (name[n])
        n++;
    if (n < 5 || name[n - 4] != '.')
        return false;
    char b = name[n - 3], m = name[n - 2], p = name[n - 1];
    if (b >= 'A' && b <= 'Z')
        b = (char)(b + 32);
    if (m >= 'A' && m <= 'Z')
        m = (char)(m + 32);
    if (p >= 'A' && p <= 'Z')
        p = (char)(p + 32);
    return b == 'b' && m == 'm' && p == 'p';
}

/* File I/O may legally complete a short read/write. */
static bool bmp_read_exact(int fd, void *buffer, size_t bytes) {
    uint8_t *p = buffer;
    while (bytes) {
        int n = vfs_read(fd, p, bytes);
        if (n <= 0 || (size_t)n > bytes)
            return false;
        p += n;
        bytes -= n;
    }
    return true;
}
static bool bmp_write_exact(int fd, const void *buffer, size_t bytes) {
    const uint8_t *p = buffer;
    while (bytes) {
        size_t chunk = bytes > WRITE_CHUNK_BYTES ? WRITE_CHUNK_BYTES : bytes;
        int n = vfs_write(fd, p, chunk);
        if (n <= 0 || (size_t)n > chunk)
            return false;
        p += n;
        bytes -= n;
    }
    return true;
}
bool camera_image_save_rgb565(const char *path, const uint16_t *pixels, int w, int h) {
    if (!path || !pixels || w <= 0 || h <= 0 || w > 1920 || h > 1080)
        return false;
    size_t stride = ((size_t)w * 2u + 3u) & ~3u;
    uint32_t data_bytes = stride * (size_t)h;
    uint8_t hdr[BMP_HDR_SIZE] = {0};
    hdr[0] = 'B';
    hdr[1] = 'M';
    put_le32(hdr + 2, BMP_HDR_SIZE + data_bytes);
    put_le32(hdr + 10, BMP_HDR_SIZE);
    put_le32(hdr + 14, 56);
    put_le32(hdr + 18, w);
    put_le32(hdr + 22, (uint32_t)(-h));
    put_le16(hdr + 26, 1);
    put_le16(hdr + 28, 16);
    put_le32(hdr + 30, 3);
    put_le32(hdr + 34, data_bytes);
    put_le32(hdr + 38, 2835);
    put_le32(hdr + 42, 2835);
    put_le32(hdr + 54, 0xf800);
    put_le32(hdr + 58, 0x07e0);
    put_le32(hdr + 62, 0x001f);
    int fd = vfs_open(path, VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
    if (fd < 0)
        return false;
    uint8_t *row = kmalloc(stride);
    bool ok = row && bmp_write_exact(fd, hdr, sizeof(hdr));
    for (int y = 0; ok && y < h; y++) {
        for (int x = 0; x < w; x++)
            put_le16(row + 2 * x, pixels[(size_t)y * w + x]);
        for (size_t i = (size_t)w * 2; i < stride; i++)
            row[i] = 0;
        ok = bmp_write_exact(fd, row, stride);
    }
    kfree(row);
    if (vfs_close(fd) < 0)
        ok = false;
    if (!ok)
        vfs_unlink(path);
    return ok;
}

bool camera_load_bmp(const char *path, uint16_t **out_pixels, int *out_w, int *out_h) {
    if (!path || !out_pixels || !out_w || !out_h)
        return false;
    *out_pixels = NULL;
    *out_w = *out_h = 0;
    int fd = vfs_open(path, VFS_O_READ);
    if (fd < 0)
        return false;
    uint8_t hdr[54], masks[12];
    uint16_t *pixels = NULL;
    uint8_t *row = NULL;
    bool ok = false;
    if (!bmp_read_exact(fd, hdr, sizeof(hdr)) || hdr[0] != 'B' || hdr[1] != 'M')
        goto done;
    uint32_t dib = get_le32(hdr + 14), offset = get_le32(hdr + 10);
    int32_t w = (int32_t)get_le32(hdr + 18), raw_h = (int32_t)get_le32(hdr + 22);
    uint16_t bpp = get_le16(hdr + 28);
    uint32_t compression = get_le32(hdr + 30);
    if ((dib != 40 && dib != 52 && dib != 56 && dib != 108 && dib != 124) ||
        get_le16(hdr + 26) != 1 || w <= 0 || w > 1920 || !raw_h || raw_h == INT32_MIN ||
        (bpp != 16 && bpp != 24) || (compression != 0 && compression != 3) ||
        (bpp == 24 && compression))
        goto done;
    bool top = raw_h < 0;
    int32_t h = top ? -raw_h : raw_h;
    if (h > 1080)
        goto done;
    uint32_t header_end = 14 + dib;
    bool rgb555 = bpp == 16 && !compression;
    if (bpp == 16 && compression == 3) {
        if (!bmp_read_exact(fd, masks, sizeof(masks)))
            goto done;
        uint32_t r = get_le32(masks), g = get_le32(masks + 4), b = get_le32(masks + 8);
        if (b != 0x001f || !((r == 0xf800 && g == 0x07e0) || (r == 0x7c00 && g == 0x03e0)))
            goto done;
        rgb555 = r == 0x7c00;
        if (dib == 40)
            header_end += 12;
    }
    size_t stride = (((size_t)w * bpp + 31u) / 32u) * 4u;
    uint32_t bytes = stride * (size_t)h;
    if (offset < header_end || offset > INT32_MAX || bytes > INT32_MAX - offset)
        goto done;
    uint32_t declared_size = get_le32(hdr + 2);
    if (declared_size && (declared_size < offset || bytes > declared_size - offset))
        goto done;
    if (vfs_seek(fd, (int32_t)offset, FS_SEEK_SET) < 0)
        goto done;
    pixels = kmalloc_psram((size_t)w * h * 2);
    if (!pixels)
        pixels = kmalloc((size_t)w * h * 2);
    row = kmalloc(stride);
    if (!pixels || !row)
        goto done;
    for (int y = 0; y < h; y++) {
        if (!bmp_read_exact(fd, row, stride))
            goto done;
        uint16_t *dst = pixels + (size_t)(top ? y : h - 1 - y) * w;
        for (int x = 0; x < w; x++) {
            if (bpp == 24)
                dst[x] = RGB565(row[x * 3 + 2], row[x * 3 + 1], row[x * 3]);
            else {
                uint16_t p = get_le16(row + x * 2);
                if (rgb555) {
                    uint16_t g = (p >> 5) & 31;
                    p = ((p & 0x7c00) << 1) | (g << 6) | ((g >> 4) << 5) | (p & 31);
                }
                dst[x] = p;
            }
        }
    }
    ok = true;
    *out_pixels = pixels;
    *out_w = w;
    *out_h = h;
done:
    vfs_close(fd);
    kfree(row);
    if (!ok)
        kfree(pixels);
    return ok;
}

bool camera_verify_bmp_file(const char *path, camera_bmp_stats_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof *out);
    if (!path) return false;
    uint16_t *pixels = NULL;
    int width = 0, height = 0;
    if (!camera_load_bmp(path, &pixels, &width, &height)) return false;
    camera_bmp_stats_t stats = {0};
    stats.width = (uint32_t)width;
    stats.height = (uint32_t)height;
    stats.pixel_count = stats.width * stats.height; /* loader bounds: <=1920*1080 */
    stats.minimum = UINT16_MAX;
    stats.fnv1a = 2166136261u;
    for (uint32_t i = 0; i < stats.pixel_count; i++) {
        uint16_t pixel = pixels[i];
        if (pixel < stats.minimum) stats.minimum = pixel;
        if (pixel > stats.maximum) stats.maximum = pixel;
        if (pixel) stats.nonzero_pixels++;
        stats.fnv1a = (stats.fnv1a ^ (uint8_t)pixel) * 16777619u;
        stats.fnv1a = (stats.fnv1a ^ (uint8_t)(pixel >> 8)) * 16777619u;
    }
    stats.nonuniform = stats.minimum != stats.maximum;
    kfree(pixels);
    *out = stats;
    return true;
}
