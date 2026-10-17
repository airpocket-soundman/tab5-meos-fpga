#include <m5os/device.h>
#include <m5os/app.h>
#include <m5os/errno.h>
#include <m5os/dev_display.h>
#include <m5os/board.h>

#define GLYPH       8
#define TAB_WIDTH   4

/* terminal state: protected by the device mutex */
static struct {
    int y0, y1;            /* pixel band */
    int scale;
    uint16_t fg, bg;
    int col, row;
} term = { 0, DISPLAY_HEIGHT, 1, COLOR_WHITE, COLOR_BLACK, 0, 0 };

static int term_cols(void) { return DISPLAY_WIDTH / (GLYPH * term.scale); }
static int term_rows(void) { return (term.y1 - term.y0) / (GLYPH * term.scale); }

static void term_clear_row(int row)
{
    display_fill_rect(0, term.y0 + row * GLYPH * term.scale, DISPLAY_WIDTH, GLYPH * term.scale, term.bg);
}

static void term_newline(void)
{
    term.col = 0;
    term.row++;
    if (term.row >= term_rows()) term.row = 0;
    term_clear_row(term.row);
}

static void term_putc(char c)
{
    if (c == '\r') return;
    if (c == '\n') { term_newline(); return; }
    if (c == '\t') { for (int i = 0; i < TAB_WIDTH; i++) term_putc(' '); return; }
    if (term.col >= term_cols()) term_newline();
    display_draw_char(term.col * GLYPH * term.scale, term.y0 + term.row * GLYPH * term.scale,
                      c, term.fg, term.bg, term.scale);
    term.col++;
}

static int display_write(device_t *dev, const void *buf, size_t len)
{
    (void)dev;
    const char *p = buf;
    for (size_t i = 0; i < len; i++) term_putc(p[i]);
    return (int)len;
}

static int display_ioctl(device_t *dev, uint32_t cmd, void *arg)
{
    (void)dev;
    if (!arg) return ERR_INVAL;
    switch (cmd) {
    case DISPLAY_IOC_GET_INFO: {
        display_info_t *i = arg;
        i->width = DISPLAY_WIDTH; i->height = DISPLAY_HEIGHT;
        return ERR_OK;
    }
    case DISPLAY_IOC_CLEAR:
        display_clear(*(uint16_t *)arg);
        term.col = 0; term.row = 0;
        return ERR_OK;
    case DISPLAY_IOC_FILL_RECT: {
        display_rect_t *r = arg;
        display_fill_rect(r->x, r->y, r->w, r->h, r->color);
        return ERR_OK;
    }
    case DISPLAY_IOC_DRAW_RECT: {
        display_rect_t *r = arg;
        display_draw_rect(r->x, r->y, r->w, r->h, r->color);
        return ERR_OK;
    }
    case DISPLAY_IOC_DRAW_TEXT: {
        display_text_t *t = arg;
        if (!t->text) return ERR_INVAL;
        display_draw_text(t->x, t->y, t->text, t->fg, t->bg, t->scale ? t->scale : 1);
        return ERR_OK;
    }
    case DISPLAY_IOC_BLIT: {
        display_blit_t *b = arg;
        if (!b->pixels) return ERR_INVAL;
        display_draw_bitmap(b->x, b->y, b->w, b->h, b->pixels);
        return ERR_OK;
    }
    case DISPLAY_IOC_SET_TERM: {
        display_term_t *t = arg;
        if (t->y0 < 0 || t->y1 > DISPLAY_HEIGHT || t->y1 <= t->y0) return ERR_INVAL;
        term.y0 = t->y0; term.y1 = t->y1;
        term.fg = t->fg; term.bg = t->bg;
        term.scale = t->scale ? t->scale : 1;
        term.col = 0; term.row = 0;
        if (term_rows() < 1) return ERR_INVAL;
        return ERR_OK;
    }
    case DISPLAY_IOC_BACKLIGHT:
        board_backlight(*(uint8_t *)arg);
        return ERR_OK;
    default:
        return ERR_INVAL;
    }
}

static const device_ops_t display_ops = { .write = display_write, .ioctl = display_ioctl };
static device_t display_device = { .name = "display", .ops = &display_ops, .required_perm = APP_PERM_DISPLAY };

void display_dev_register(void) { device_register(&display_device); }
