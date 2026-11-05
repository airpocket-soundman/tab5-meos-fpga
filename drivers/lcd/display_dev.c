#include <m5os/device.h>
#include <m5os/app.h>
#include <m5os/errno.h>
#include <m5os/dev_display.h>
#include <m5os/board.h>
#include <m5os/task.h>
#if M5OS_BOARD_TAB5
#include "fbcon.h"
#endif

/* Do not let a second device caller wait on the retained display batch mutex
 * while holding device.lock: that would block the owner's END ioctl forever. */
static task_t *lease_owner;
static uint32_t lease_id;
static bool lease_paused,lease_previous_pause,lease_frame;
#if M5OS_BOARD_TAB5
bool display_render_busy(void);
#endif
bool display_suspend_busy(void)
{
    /* Do not reap/mutate a lease here: suspend only asks whether it is safe.
     * A dead lease is cleaned by normal device-close/repaint paths. */
    bool live=lease_owner&&lease_owner->id==lease_id&&
        lease_owner->state!=TASK_TERMINATED&&lease_owner->state!=TASK_UNUSED;
#if M5OS_BOARD_TAB5
    return live||fbcon_is_paused()||display_render_busy();
#else
    return live;
#endif
}
static bool device_frame_begin(void){
#if M5OS_BOARD_TAB5
    return display_begin_frame();
#else
    return false;
#endif
}
static bool device_frame_end(void){
#if M5OS_BOARD_TAB5
    return display_end_frame();
#else
    return false;
#endif
}
static bool lease_is_self(void){return lease_owner==task_current()&&lease_owner&&lease_owner->id==lease_id;}
static void lease_restore(void)
{
#if M5OS_BOARD_TAB5
    if(lease_paused)fbcon_pause(lease_previous_pause);
#endif
    lease_owner=NULL;lease_id=0;lease_paused=false;lease_frame=false;
}
static void lease_reap(void)
{
    if(lease_owner&&(lease_owner->id!=lease_id||lease_owner->state==TASK_TERMINATED||lease_owner->state==TASK_UNUSED))
        /* The display backend already reaps a killed task's retained frame on
         * the next draw after task-kill releases its mutex. Never end another
         * task's frame or dereference application-owned render storage here. */
        lease_restore();
}
static int display_close(device_t *dev)
{
    (void)dev;lease_reap();
    if(lease_is_self()) {
        int error=lease_frame&&!device_frame_end()?ERR_IO:0;
        lease_restore();return error;
    }
    return 0;
}

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
    lease_reap();if(lease_owner&&!lease_is_self())return ERR_BUSY;
    const char *p = buf;
    for (size_t i = 0; i < len; i++) term_putc(p[i]);
    return (int)len;
}

static int display_ioctl(device_t *dev, uint32_t cmd, void *arg)
{
    (void)dev;
    lease_reap();
    if(lease_owner&&!lease_is_self()&&cmd!=DISPLAY_IOC_GET_INFO)return ERR_BUSY;
    if(cmd==DISPLAY_IOC_BEGIN_FRAME) {
        if(!task_current()||lease_frame)return ERR_BUSY;
        if(!device_frame_begin())return ERR_BUSY;
        lease_owner=task_current();lease_id=lease_owner->id;lease_frame=true;return 0;
    }
    if(cmd==DISPLAY_IOC_END_FRAME) {
        if(!lease_is_self()||!lease_frame)return ERR_INVAL;
        bool ok=device_frame_end();lease_frame=false;
        if(!lease_paused)lease_restore();
        return ok?0:ERR_IO;
    }
    if(cmd==DISPLAY_IOC_CONSOLE_PAUSE) {
#if M5OS_BOARD_TAB5
        if(!arg||!task_current())return ERR_INVAL;
        if(*(bool*)arg) {
            if(!lease_paused) {
                lease_owner=task_current();lease_id=lease_owner->id;
                lease_previous_pause=fbcon_is_paused();lease_paused=true;fbcon_pause(true);
            }
        } else {
            if(lease_frame)return ERR_BUSY;
            if(lease_is_self())lease_restore();
        }
        return 0;
#else
        return ERR_NOSYS;
#endif
    }
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
    case DISPLAY_IOC_BLIT_SCALED: {
        display_scaled_blit_t *b=arg;
        if(!b->image.pixels||b->scale<1||b->scale>16||b->image.w<=0||b->image.h<=0||
           b->image.w>DISPLAY_WIDTH/b->scale||b->image.h>DISPLAY_HEIGHT/b->scale)return ERR_INVAL;
        display_draw_bitmap_scaled(b->image.x,b->image.y,b->image.w,b->image.h,b->image.pixels,b->scale);
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

static const device_ops_t display_ops = { .close=display_close,.write = display_write, .ioctl = display_ioctl };
static device_t display_device = { .name = "display", .ops = &display_ops, .required_perm = APP_PERM_DISPLAY };

void display_dev_register(void) { device_register(&display_device); }
