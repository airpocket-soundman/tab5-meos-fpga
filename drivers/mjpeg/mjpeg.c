/* Baseline JPEG decoder with reduced-size decoding; see mjpeg.h.
 * Written for the Tab5 USB camera path (Motion-JPEG frames) and kept free of kernel dependencies so that it can
 * be tested on the host against Pillow (tests/mjpeg). */
#include "mjpeg.h"
#ifndef M5OS_MODULE_MJPEG
#define M5OS_MODULE_MJPEG 1 /* host build */
#endif
#if M5OS_MODULE_MJPEG
#include <string.h>

#include "mjpeg_tables.inc"

typedef struct {
    uint8_t bits[17];
    uint8_t vals[256];
    int32_t maxcode[18];
    int32_t mincode[17];
    int16_t valptr[17];
    uint16_t look[512]; /* 9-bit lookahead: (length << 8) | symbol, 0 = longer code */
    bool defined;
} huff_t;

typedef struct {
    uint8_t id, h, v, tq, td, ta;
} comp_t;

typedef struct {
    const uint8_t *p, *end;
    uint32_t bitbuf;
    int bitcnt;
    int injected;     /* zero bytes fed after the data or a marker */
    bool marker_hit;
    bool error;
    uint16_t qt[4][64]; /* indexed by zigzag position */
    bool qt_defined[4];
    huff_t dc[2], ac[2];
    int width, height, ncomp;
    comp_t comp[3];
    unsigned restart_interval;
    int dcpred[3];
    bool have_sof;
} dec_t;

static dec_t D;

static const uint8_t zigzag[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

/* ---- Huffman tables ---- */

static bool huff_build(huff_t *h) {
    int32_t code = 0;
    int k = 0;
    memset(h->look, 0, sizeof h->look);
    for (int l = 1; l <= 16; l++) {
        h->valptr[l] = (int16_t)k;
        h->mincode[l] = code;
        code += h->bits[l];
        k += h->bits[l];
        if (code > (1 << l))
            return false; /* over-subscribed */
        h->maxcode[l] = h->bits[l] ? code - 1 : -1;
        if (l <= 9) {
            for (int c = h->mincode[l]; c <= h->maxcode[l]; c++) {
                int sym = h->vals[h->valptr[l] + (c - h->mincode[l])];
                int base = c << (9 - l);
                for (int i = 0; i < (1 << (9 - l)); i++)
                    h->look[base + i] = (uint16_t)((l << 8) | sym);
            }
        }
        code <<= 1;
    }
    h->maxcode[17] = 0x7fffffff;
    h->defined = true;
    return true;
}

static void huff_load_std(huff_t *h, const uint8_t bits16[16], const uint8_t *vals, size_t n) {
    memset(h, 0, sizeof *h);
    for (int i = 0; i < 16; i++)
        h->bits[i + 1] = bits16[i];
    memcpy(h->vals, vals, n);
    huff_build(h);
}

static void use_standard_tables(void) {
    if (!D.dc[0].defined)
        huff_load_std(&D.dc[0], std_dc_lum_bits, std_dc_lum_vals, sizeof std_dc_lum_vals);
    if (!D.dc[1].defined)
        huff_load_std(&D.dc[1], std_dc_chr_bits, std_dc_chr_vals, sizeof std_dc_chr_vals);
    if (!D.ac[0].defined)
        huff_load_std(&D.ac[0], std_ac_lum_bits, std_ac_lum_vals, sizeof std_ac_lum_vals);
    if (!D.ac[1].defined)
        huff_load_std(&D.ac[1], std_ac_chr_bits, std_ac_chr_vals, sizeof std_ac_chr_vals);
}

/* ---- header parsing ---- */

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

/* Parses markers up to and including SOS (or SOF when stop_at_sof).  Returns the entropy-coded data start, or
 * NULL on error / unsupported. */
static const uint8_t *parse_headers(const uint8_t *data, size_t len, bool stop_at_sof) {
    memset(&D, 0, sizeof D);
    const uint8_t *p = data, *end = data + len;
    if (len < 4 || p[0] != 0xFF || p[1] != 0xD8)
        return NULL;
    p += 2;
    for (;;) {
        while (p < end && *p != 0xFF)
            p++;
        while (p < end && *p == 0xFF)
            p++;
        if (p >= end)
            return NULL;
        uint8_t m = *p++;
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7) || m == 0x00)
            continue; /* no length */
        if (m == 0xD9)
            return NULL; /* EOI before SOS */
        if (end - p < 2)
            return NULL;
        size_t seg = be16(p);
        if (seg < 2 || (size_t)(end - p) < seg)
            return NULL;
        const uint8_t *s = p + 2, *se = p + seg;
        switch (m) {
        case 0xDB: /* DQT */
            while (s < se) {
                unsigned pq = s[0] >> 4, tq = s[0] & 15;
                s++;
                if (tq > 3 || pq > 1 || se - s < (pq ? 128 : 64))
                    return NULL;
                for (int i = 0; i < 64; i++) {
                    D.qt[tq][i] = pq ? be16(s) : s[0];
                    s += pq ? 2 : 1;
                }
                D.qt_defined[tq] = true;
            }
            break;
        case 0xC4: /* DHT */
            while (s < se) {
                if (se - s < 17)
                    return NULL;
                unsigned tc = s[0] >> 4, th = s[0] & 15;
                if (tc > 1 || th > 1)
                    return NULL;
                huff_t *h = tc ? &D.ac[th] : &D.dc[th];
                memset(h, 0, sizeof *h);
                int total = 0;
                for (int i = 1; i <= 16; i++) {
                    h->bits[i] = s[i];
                    total += s[i];
                }
                s += 17;
                if (total > 256 || se - s < total)
                    return NULL;
                memcpy(h->vals, s, (size_t)total);
                s += total;
                if (!huff_build(h))
                    return NULL;
            }
            break;
        case 0xC0:
        case 0xC1: /* SOF baseline / extended sequential */
            if (seg < 8 || s[0] != 8)
                return NULL;
            D.height = be16(s + 1);
            D.width = be16(s + 3);
            D.ncomp = s[5];
            if (D.width < 1 || D.height < 1 || (D.ncomp != 1 && D.ncomp != 3) || seg < (size_t)8 + 3 * D.ncomp)
                return NULL;
            for (int i = 0; i < D.ncomp; i++) {
                D.comp[i].id = s[6 + 3 * i];
                D.comp[i].h = s[7 + 3 * i] >> 4;
                D.comp[i].v = s[7 + 3 * i] & 15;
                D.comp[i].tq = s[8 + 3 * i];
                if (D.comp[i].h < 1 || D.comp[i].h > 2 || D.comp[i].v < 1 || D.comp[i].v > 2 || D.comp[i].tq > 3)
                    return NULL;
            }
            D.have_sof = true;
            if (stop_at_sof)
                return p;
            break;
        case 0xC2: case 0xC3: case 0xC5: case 0xC6: case 0xC7: case 0xC9: case 0xCA: case 0xCB: case 0xCD:
        case 0xCE: case 0xCF:
            return NULL; /* progressive, lossless, arithmetic, hierarchical */
        case 0xDD: /* DRI */
            if (seg < 4)
                return NULL;
            D.restart_interval = be16(s);
            break;
        case 0xDA: { /* SOS */
            if (!D.have_sof || seg < 6 || s[0] != D.ncomp || seg < (size_t)6 + 2 * D.ncomp)
                return NULL;
            for (int i = 0; i < D.ncomp; i++) {
                int found = -1;
                for (int c = 0; c < D.ncomp; c++)
                    if (D.comp[c].id == s[1 + 2 * i])
                        found = c;
                if (found < 0)
                    return NULL;
                D.comp[found].td = s[2 + 2 * i] >> 4;
                D.comp[found].ta = s[2 + 2 * i] & 15;
                if (D.comp[found].td > 1 || D.comp[found].ta > 1)
                    return NULL;
            }
            return p + seg;
        }
        default:
            break; /* APPn, COM, ... */
        }
        p += seg;
    }
}

bool mjpeg_info(const uint8_t *data, size_t len, mjpeg_info_t *info) {
    if (!data || !info || !parse_headers(data, len, true))
        return false;
    info->width = D.width;
    info->height = D.height;
    info->components = D.ncomp;
    return true;
}

unsigned mjpeg_shift_for(int width, int height, int max_w, int max_h) {
    for (unsigned s = 0; s < 3; s++) {
        int n = 8 >> s;
        if ((width * n + 7) / 8 <= max_w && (height * n + 7) / 8 <= max_h)
            return s;
    }
    return 3;
}

/* ---- bit reader ---- */

static void fill(void) {
    while (D.bitcnt <= 24) {
        uint8_t b = 0;
        if (D.marker_hit || D.p >= D.end) {
            D.injected++;
        } else {
            b = *D.p;
            if (b == 0xFF) {
                if (D.p + 1 >= D.end || D.p[1] != 0x00) {
                    D.marker_hit = true; /* a marker (RSTn, EOI...) ends the data: feed zeros */
                    b = 0;
                    D.injected++;
                } else {
                    D.p += 2;
                }
            } else {
                D.p++;
            }
        }
        D.bitbuf = (D.bitbuf << 8) | b;
        D.bitcnt += 8;
    }
}

static inline int getbits(int n) {
    if (n == 0)
        return 0;
    if (D.bitcnt < n)
        fill();
    D.bitcnt -= n;
    return (int)((D.bitbuf >> D.bitcnt) & ((1u << n) - 1u));
}

static int huff_decode(const huff_t *h) {
    if (D.bitcnt < 16)
        fill();
    unsigned peek = (D.bitbuf >> (D.bitcnt - 9)) & 0x1FFu;
    unsigned e = h->look[peek];
    if (e) {
        D.bitcnt -= (int)(e >> 8);
        return (int)(e & 0xFF);
    }
    for (int l = 10; l <= 16; l++) {
        int32_t code = (int32_t)((D.bitbuf >> (D.bitcnt - l)) & ((1u << l) - 1u));
        if (h->maxcode[l] >= 0 && code <= h->maxcode[l] && code >= h->mincode[l]) {
            D.bitcnt -= l;
            return h->vals[h->valptr[l] + (code - h->mincode[l])];
        }
    }
    D.error = true;
    D.bitcnt -= 16;
    return 0;
}

static inline int extend(int v, int n) { return v < (1 << (n - 1)) ? v - (1 << n) + 1 : v; }

/* ---- inverse DCT (N = 8, 4, 2, 1 points of the low-frequency coefficients) ---- */

static inline uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

/* F: dequantised coefficients, natural order, stride 8.  Writes N x N samples (level shifted, clamped).
 * rows / cols: only coefficients with row < rows and column < cols can be non-zero (most blocks are sparse). */
static void idct_scaled(const int32_t *F, unsigned n, unsigned rows, unsigned cols, uint8_t *dst, int stride) {
    const int16_t *M = n == 8 ? &idct_m8[0][0] : n == 4 ? &idct_m4[0][0] : &idct_m2[0][0];
    int32_t tmp[64];
    int final_shift = n == 8 ? 11 : n == 4 ? 12 : 13;
    for (unsigned v = 0; v < rows; v++) {
        const int32_t *row = &F[v * 8];
        for (unsigned x = 0; x < n; x++) {
            int32_t sum = 0;
            for (unsigned u = 0; u < cols; u++)
                sum += M[x * n + u] * row[u];
            tmp[v * n + x] = (sum + 1024) >> 11;
        }
    }
    for (unsigned y = 0; y < n; y++) {
        for (unsigned x = 0; x < n; x++) {
            int32_t sum = 0;
            for (unsigned v = 0; v < rows; v++)
                sum += M[y * n + v] * tmp[v * n + x];
            dst[y * stride + x] = clamp8(((sum + (1 << (final_shift - 1))) >> final_shift) + 128);
        }
    }
}

/* ---- block and MCU decoding ---- */

#define TILE 16

/* Decodes one 8x8 block of component c and stores its N x N samples at dst. */
static void decode_block(int c, unsigned n, uint8_t *dst) {
    int32_t F[64];
    const uint16_t *q = D.qt[D.comp[c].tq];
    memset(F, 0, sizeof F);
    int t = huff_decode(&D.dc[D.comp[c].td]);
    int diff = t ? extend(getbits(t & 15), t & 15) : 0;
    D.dcpred[c] += diff;
    F[0] = D.dcpred[c] * q[0];
    bool ac = false;
    unsigned maxr = 0, maxc = 0;
    for (int k = 1; k < 64;) {
        int rs = huff_decode(&D.ac[D.comp[c].ta]);
        int r = rs >> 4, s = rs & 15;
        if (s == 0) {
            if (r == 15) {
                k += 16;
                continue;
            }
            break;
        }
        k += r;
        if (k > 63) {
            D.error = true;
            break;
        }
        int v = extend(getbits(s), s);
        unsigned pos = zigzag[k];
        if ((pos & 7) < n && (pos >> 3) < n) {
            F[pos] = v * q[k];
            ac = true;
            if ((pos & 7) > maxc) maxc = pos & 7;
            if ((pos >> 3) > maxr) maxr = pos >> 3;
        }
        k++;
    }
    if (!ac || n == 1) {
        uint8_t v = clamp8(((F[0] + 4) >> 3) + 128); /* DC only: a flat N x N block (the DC term is 8x the mean) */
        for (unsigned y = 0; y < n; y++)
            memset(&dst[y * TILE], v, n);
        return;
    }
    idct_scaled(F, n, maxr + 1, maxc + 1, dst, TILE);
}

static inline uint16_t rgb565(int y, int cb, int cr) {
    cb -= 128;
    cr -= 128;
    int r = y + ((91881 * cr) >> 16);
    int g = y - ((22554 * cb + 46802 * cr) >> 16);
    int b = y + ((116130 * cb) >> 16);
    return (uint16_t)(((clamp8(r) & 0xF8) << 8) | ((clamp8(g) & 0xFC) << 3) | (clamp8(b) >> 3));
}

/* After a restart interval: find the RSTn marker, reset the predictors. */
static bool restart_sync(void) {
    const uint8_t *p = D.p;
    while (p + 1 < D.end) {
        if (p[0] == 0xFF && p[1] >= 0xD0 && p[1] <= 0xD7) {
            D.p = p + 2;
            D.bitbuf = 0;
            D.bitcnt = 0;
            D.injected = 0;
            D.marker_hit = false;
            memset(D.dcpred, 0, sizeof D.dcpred);
            return true;
        }
        if (p[0] == 0xFF && p[1] == 0x00) {
            p += 2;
        } else if (p[0] == 0xFF && p[1] != 0xFF) {
            return false; /* some other marker */
        } else {
            p++;
        }
    }
    return false;
}

bool mjpeg_decode_rgb565(const uint8_t *data, size_t len, unsigned shift, uint16_t *out, size_t out_pixels,
                         int *out_w, int *out_h) {
    if (!data || !out || shift > 3)
        return false;
    const uint8_t *scan = parse_headers(data, len, false);
    if (!scan || !D.have_sof)
        return false;
    for (int c = 0; c < D.ncomp; c++)
        if (!D.qt_defined[D.comp[c].tq])
            return false;
    use_standard_tables();

    int hmax = 1, vmax = 1;
    for (int c = 0; c < D.ncomp; c++) {
        if (D.comp[c].h > hmax) hmax = D.comp[c].h;
        if (D.comp[c].v > vmax) vmax = D.comp[c].v;
    }
    for (int c = 0; c < D.ncomp; c++)
        if (hmax % D.comp[c].h || vmax % D.comp[c].v)
            return false;
    if (D.ncomp == 1) {
        hmax = vmax = 1; /* a single component is not subsampled whatever the header says */
        D.comp[0].h = D.comp[0].v = 1;
    }

    unsigned n = 8u >> shift;
    int ow = (D.width * (int)n + 7) / 8, oh = (D.height * (int)n + 7) / 8;
    if ((size_t)ow * (size_t)oh > out_pixels)
        return false;
    int mcus_x = (D.width + hmax * 8 - 1) / (hmax * 8), mcus_y = (D.height + vmax * 8 - 1) / (vmax * 8);
    int tw = hmax * (int)n, th = vmax * (int)n;

    D.p = scan;
    D.end = data + len;
    D.bitbuf = 0;
    D.bitcnt = 0;
    D.injected = 0;
    D.marker_hit = false;
    D.error = false;
    memset(D.dcpred, 0, sizeof D.dcpred);

    uint8_t tiles[3][TILE][TILE];
    unsigned since_restart = 0;
    unsigned shx[3] = {0, 0, 0}, shy[3] = {0, 0, 0}; /* component sample = tile pixel >> shift */
    for (int c = 0; c < D.ncomp; c++) {
        shx[c] = hmax / D.comp[c].h - 1;
        shy[c] = vmax / D.comp[c].v - 1;
    }
    for (int my = 0; my < mcus_y; my++) {
        for (int mx = 0; mx < mcus_x; mx++) {
            for (int c = 0; c < D.ncomp; c++)
                for (int by = 0; by < D.comp[c].v; by++)
                    for (int bx = 0; bx < D.comp[c].h; bx++)
                        decode_block(c, n, &tiles[c][by * (int)n][bx * (int)n]);
            if (D.error)
                return false;

            int x0 = mx * tw, y0 = my * th;
            for (int py = 0; py < th && y0 + py < oh; py++) {
                uint16_t *dst = &out[(size_t)(y0 + py) * ow + x0];
                int w = tw;
                if (x0 + w > ow)
                    w = ow - x0;
                if (D.ncomp == 1) {
                    for (int px = 0; px < w; px++) {
                        int y = tiles[0][py][px];
                        dst[px] = (uint16_t)(((y & 0xF8) << 8) | ((y & 0xFC) << 3) | (y >> 3));
                    }
                } else {
                    const uint8_t *yrow = tiles[0][py >> shy[0]];
                    const uint8_t *brow = tiles[1][py >> shy[1]];
                    const uint8_t *rrow = tiles[2][py >> shy[2]];
                    for (int px = 0; px < w; px++)
                        dst[px] = rgb565(yrow[px >> shx[0]], brow[px >> shx[1]], rrow[px >> shx[2]]);
                }
            }

            if (D.restart_interval && ++since_restart == D.restart_interval && (mx + 1 < mcus_x || my + 1 < mcus_y)) {
                since_restart = 0;
                if (!restart_sync())
                    return false;
            }
        }
    }
    if (D.injected * 8 > D.bitcnt)
        return false; /* ran past the end of the data: truncated frame */
    if (out_w) *out_w = ow;
    if (out_h) *out_h = oh;
    return true;
}
#endif
