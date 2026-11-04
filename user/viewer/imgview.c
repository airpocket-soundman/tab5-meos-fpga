#include "imgview.h"
#include "../tui/dqwin.h"
#include "../camera/camera_app.h"
#include <limits.h>
#include <m5os/display.h>
#include <m5os/vfs.h>
#include <m5os/kmalloc.h>
#include <m5os/errno.h>
#include <m5os/kprintf.h>

#define MAX_FILE   (8u * 1024u * 1024u)
#define MAX_PIXELS (4 * 1024 * 1024)      /* a 16-bit copy of the biggest picture must fit in PSRAM */
#define LINE_BYTES 4                      /* BMP rows are padded to a multiple of 4 bytes */

/* BMP ownership stays with the tested shared camera/image decoder. PPM/PGM
 * need only a bounded raw raster view, never BMP palette/mask parsing here. */
typedef struct { const uint8_t *data; int w,h; size_t stride; bool gray; } src_t;
static uint16_t pixel(const src_t *s,int x,int y)
{
    const uint8_t *p=s->data+(size_t)y*s->stride+(size_t)x*(s->gray?1u:3u);
    return s->gray?RGB565(p[0],p[0],p[0]):RGB565(p[0],p[1],p[2]);
}
static bool whitespace(uint8_t c){return c==' '||c=='\n'||c=='\r'||c=='\t'||c=='\v'||c=='\f';}

static const uint8_t *ppm_int(const uint8_t *p, const uint8_t *end, int *out)
{
    while (p < end && (whitespace(*p) || *p == '#')) {
        if (*p == '#') while (p < end && *p != '\n') p++; else p++;                   /* comments run to end of line */
    }
    int v = 0;
    if (p >= end || *p < '0' || *p > '9') return NULL;
    while (p < end && *p >= '0' && *p <= '9') {
        unsigned digit=(unsigned)(*p++-'0');
        if(v>(INT_MAX-(int)digit)/10)return NULL;
        v=v*10+(int)digit;
    }
    *out = v;
    return p;
}

static bool parse_ppm(const uint8_t *f, size_t n, src_t *s)
{
    if (n < 10 || f[0] != 'P' || (f[1] != '6' && f[1] != '5')) return false;
    bool color = f[1] == '6';
    const uint8_t *p = f + 2, *end = f + n;
    int w, h, maxv;
    if (!(p = ppm_int(p, end, &w)) || !(p = ppm_int(p, end, &h)) || !(p = ppm_int(p, end, &maxv))) return false;
    if (w <= 0 || h <= 0 || maxv != 255 || p >= end || !whitespace(*p)) return false;
    if((unsigned)w>MAX_PIXELS/(unsigned)h)return false;
    uint8_t separator=*p++;
    if(separator=='\r'&&p<end&&*p=='\n')p++; /* Conventional CRLF header. */
    size_t stride=(size_t)w*(color?3u:1u),bytes=stride*(size_t)h;
    if(bytes>(size_t)(end-p))return false;
    s->w=w;s->h=h;s->data=p;s->stride=stride;s->gray=!color;
    return true;
}

static const char *s_why = "";
const char *imgview_last_error(void) { return s_why; }

bool imgview_is_image_name(const char *name)
{
    if (!name) return false;
    int n = 0;
    while (name[n]) n++;
    if (n < 5 || name[n - 4] != '.') return false;
    char a = name[n - 3], b = name[n - 2], c = name[n - 1];
    a = (char)(a >= 'A' && a <= 'Z' ? a + 32 : a);
    b = (char)(b >= 'A' && b <= 'Z' ? b + 32 : b);
    c = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return (a == 'b' && b == 'm' && c == 'p') || (a == 'p' && (b == 'p' || b == 'g') && c == 'm');
}

bool imgview_load(const char *path, uint16_t **pixels, int *ow, int *oh)
{
    if(!path||!pixels||!ow||!oh){s_why="invalid output arguments";return false;}
    vfs_info_t st;
    *pixels = NULL;*ow=*oh=0;
    s_why = "";
    if (vfs_stat(path, &st) != ERR_OK) { s_why = "cannot stat the file"; return false; }
    if (st.type != FS_TYPE_FILE)       { s_why = "not a regular file"; return false; }
    if (st.size < 3)                  { s_why = "file too small"; return false; }
    if (st.size > MAX_FILE)            { s_why = "file larger than 8 MB"; return false; }
    /* Sniff without allocating the entire BMP. The shared decoder performs its
     * own exact reads, dimensions/format/offset checks and bounded allocation. */
    uint8_t magic[2];size_t peeked=0;
    int peek=vfs_open(path,VFS_O_READ);
    if(peek>=0){while(peeked<sizeof magic){int n=vfs_read(peek,magic+peeked,sizeof magic-peeked);if(n<=0||(size_t)n>sizeof magic-peeked)break;peeked+=(size_t)n;}vfs_close(peek);}
    if(peeked!=sizeof magic){s_why="short read";return false;}
    if(magic[0]=='B'&&magic[1]=='M'){
        if(camera_load_bmp(path,pixels,ow,oh))return true;
        s_why="unsupported or malformed BMP (shared decoder: 16/24-bit)";return false;
    }
    uint8_t *file = kmalloc_psram(st.size);
    if (!file)                         { s_why = "out of PSRAM"; return false; }
    int fd = vfs_open(path, VFS_O_READ);
    size_t got = 0;
    if (fd >= 0) {
        int r;
        while (got < st.size && (r = vfs_read(fd, file + got, st.size - got)) > 0) {
            if((size_t)r>st.size-got)break;
            got += (size_t)r;
        }
        vfs_close(fd);
    }
    src_t s;
    uint16_t *out = NULL;
    if (got != st.size) s_why = "short read";
    else if (!parse_ppm(file, got, &s)) s_why = "not a supported BMP/PPM";
    else if ((int64_t)s.w * s.h > MAX_PIXELS) s_why = "picture too large";
    else if (!(out = kmalloc_psram((size_t)s.w * (size_t)s.h * 2))) s_why = "out of PSRAM for the picture";
    else {
        for (int y = 0; y < s.h; y++)
            for (int x = 0; x < s.w; x++) out[(size_t)y * (size_t)s.w + (size_t)x] = pixel(&s, x, y);
        *ow = s.w; *oh = s.h;
    }
    kfree(file);
    *pixels = out;
    return out != NULL;
}

void imgview_draw_fit(int x, int y, int dw, int dh, const uint16_t *src, int sw, int sh)
{
    static uint16_t *buf;                                    /* scaled copy, at most one screenful, allocated once */
    static int xmap[DISPLAY_WIDTH];
    if (!src || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    if (dw > DISPLAY_WIDTH) dw = DISPLAY_WIDTH;
    if (dh > DISPLAY_HEIGHT) dh = DISPLAY_HEIGHT;
    if (dw == sw && dh == sh) { display_draw_bitmap(x, y, sw, sh, src); return; }
    if (!buf) buf = kmalloc_psram((size_t)DISPLAY_WIDTH * DISPLAY_HEIGHT * 2);
    if (!buf) { display_draw_bitmap(x, y, sw < dw ? sw : dw, sh < dh ? sh : dh, src); return; }
    for (int i = 0; i < dw; i++) xmap[i] = (int)((int64_t)i * sw / dw);
    for (int j = 0; j < dh; j++) {
        const uint16_t *row = &src[(size_t)((int64_t)j * sh / dh) * (size_t)sw];
        uint16_t *out = &buf[(size_t)j * (size_t)dw];
        for (int i = 0; i < dw; i++) out[i] = row[xmap[i]];
    }
    display_draw_bitmap(x, y, dw, dh, buf);
}

/* ---- the viewer ------------------------------------------------------------------ */

#define V_HEADER_ROWS 3
#define V_FOOTER_ROWS 4
#define V_CELL        (8 * M5OS_FBCON_SCALE)

static void pad_text(int cx, int cy, const char *s)           /* write s, blanking the rest of the line inside the frame */
{
    char line[200];
    int n = 0, w = dq_cols() - cx - 2;
    while (s[n] && n < w && n < (int)sizeof line - 1) { line[n] = s[n]; n++; }
    while (n < w && n < (int)sizeof line - 1) line[n++] = ' ';
    line[n] = 0;
    dq_text(cx, cy, line);
}

bool imgview_show(const char *start, const imgview_nav_t *nav)
{
    char path[VFS_PATH_MAX];
    ksnprintf(path, sizeof path, "%s", start);
    bool fit = true;                                           /* Z toggles between "fill the area" and the real size */
    int cols = dq_cols(), rows = dq_rows(), fy = rows - V_FOOTER_ROWS, mid_rows = rows - V_HEADER_ROWS - V_FOOTER_ROWS;

    for (;;) {                                                 /* one pass per image */
        uint16_t *px;
        int w, h;
        if (!imgview_load(path, &px, &w, &h)) { dq_message("Cannot show this file:", s_why); return false; }
        vfs_info_t st;
        unsigned kb = vfs_stat(path, &st) == ERR_OK ? (unsigned)(st.size / 1024u) : 0;
        const char *name = path;
        for (const char *q = path; *q; q++) if (*q == '/') name = q + 1;

        dq_window(0, 0, cols, V_HEADER_ROWS);
        dq_window(0, V_HEADER_ROWS, cols, mid_rows);
        dq_window(0, fy, cols, V_FOOTER_ROWS);
        dq_text(2, 1, "IMAGE VIEWER");
        char help[140];
        ksnprintf(help, sizeof help, "Z/Space:Zoom  %s%sQ/ESC/Enter:Close",
                  (nav && nav->step) ? "Left/Right:Prev/Next  " : "", (nav && nav->camera) ? "P:Camera  " : "");
        dq_text(2, fy + 1, help);
        pad_text(2, fy + 2, path);

        bool changed_image = false;
        while (!changed_image) {
            char info[140];
            ksnprintf(info, sizeof info, "%s  (%dx%d, %u KB)  [%s]", name, w, h, kb, fit ? "Fit" : "1:1");
            pad_text(16, 1, info);

            int area_w = DISPLAY_WIDTH - 2 * V_CELL, area_h = mid_rows * V_CELL - V_CELL;
            int dw, dh;
            if (fit) {                                         /* scale up or down to fill the area, keeping the proportions */
                dw = area_w; dh = (int)((int64_t)h * dw / w);
                if (dh > area_h) { dh = area_h; dw = (int)((int64_t)w * dh / h); }
            } else {
                dw = w; dh = h;
                if (dw > area_w) { dh = (int)((int64_t)dh * area_w / dw); dw = area_w; }
                if (dh > area_h) { dw = (int)((int64_t)dw * area_h / dh); dh = area_h; }
            }
            if (dw < 1) dw = 1;
            if (dh < 1) dh = 1;
            int dx = (DISPLAY_WIDTH - dw) / 2, dy = V_HEADER_ROWS * V_CELL + (mid_rows * V_CELL - dh) / 2;
            display_fill_rect(V_CELL / 2, V_HEADER_ROWS * V_CELL + V_CELL / 2, DISPLAY_WIDTH - V_CELL, mid_rows * V_CELL - V_CELL, COLOR_BLACK);
            display_draw_rect(dx - 2, dy - 2, dw + 4, dh + 4, COLOR_WHITE);
            imgview_draw_fit(dx, dy, dw, dh, px, w, h);

            int k = dq_getkey();
            if (k == 'q' || k == 'Q' || k == DQK_ESC || k == DQK_ENTER) { kfree(px); return true; }
            if (k == DQK_REFRESH) { changed_image = true; continue; }     /* reload and repaint everything */
            if (k == 'z' || k == 'Z' || k == ' ') { fit = !fit; continue; }
            if ((k == 'p' || k == 'P') && nav && nav->camera) { kfree(px); nav->camera(); return true; }
            int dir = (k == DQK_RIGHT || k == DQK_DOWN || k == 'l' || k == 'j' || k == 'n') ? 1
                    : (k == DQK_LEFT || k == DQK_UP || k == 'h' || k == 'k') ? -1 : 0;
            char next[VFS_PATH_MAX];
            if (dir && nav && nav->step) {
                ksnprintf(next, sizeof next, "%s", path);
                if (nav->step(dir, next, sizeof next)) { ksnprintf(path, sizeof path, "%s", next); changed_image = true; }
            }
        }
        kfree(px);
    }
}

int imgview_write_bmp(const char *path, const uint16_t *px, int w, int h)
{
    if(!path||!px||w<=0||h<=0||(unsigned)w>MAX_PIXELS/(unsigned)h)return ERR_INVAL;
    size_t row_bytes=(size_t)w*3u;
    if(row_bytes>INT_MAX-3)return ERR_INVAL;
    int stride=(int)((row_bytes+3u)&~(size_t)3u);
    if((uint64_t)(unsigned)stride*(unsigned)h>UINT32_MAX-54u)return ERR_INVAL;
    uint32_t data = (uint32_t)stride * (uint32_t)h, total = 54 + data;
    uint8_t hdr[54] = { 'B', 'M' };
    hdr[2] = (uint8_t)total; hdr[3] = (uint8_t)(total >> 8); hdr[4] = (uint8_t)(total >> 16); hdr[5] = (uint8_t)(total >> 24);
    hdr[10] = 54; hdr[14] = 40;
    hdr[18] = (uint8_t)w; hdr[19] = (uint8_t)(w >> 8); hdr[20] = (uint8_t)(w >> 16);
    hdr[22] = (uint8_t)h; hdr[23] = (uint8_t)(h >> 8); hdr[24] = (uint8_t)(h >> 16);
    hdr[26] = 1; hdr[28] = 24;
    hdr[34] = (uint8_t)data; hdr[35] = (uint8_t)(data >> 8); hdr[36] = (uint8_t)(data >> 16); hdr[37] = (uint8_t)(data >> 24);
    int fd = vfs_open(path, VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
    if (fd < 0) return fd;
    int rc = vfs_write(fd, hdr, sizeof hdr) == (int)sizeof hdr ? 0 : ERR_NOSPC;
    uint8_t *row = kmalloc((size_t)stride);
    if (!row) { vfs_close(fd); return ERR_NOMEM; }
    for (int y = h - 1; y >= 0 && rc == 0; y--) {                    /* bottom-up, as BMP wants */
        for (int x = 0; x < w; x++) {
            uint16_t c = px[(size_t)y * (size_t)w + (size_t)x];
            row[x * 3 + 0] = (uint8_t)((c & 0x1F) << 3);              /* B */
            row[x * 3 + 1] = (uint8_t)(((c >> 5) & 0x3F) << 2);       /* G */
            row[x * 3 + 2] = (uint8_t)(((c >> 11) & 0x1F) << 3);      /* R */
        }
        for (int i = w * 3; i < stride; i++) row[i] = 0;
        if (vfs_write(fd, row, (size_t)stride) != stride) rc = ERR_NOSPC;
    }
    kfree(row);
    vfs_close(fd);
    if (rc) vfs_unlink(path);
    return rc;
}
