#include "fbcon.h"
#include <m5os/display.h>
#include <m5os/kprintf.h>
#include <m5os/task.h>
#include <m5os/hal_time.h>
#include <m5os/arch.h>
#include <m5os/kmalloc.h>

#define SCALE     M5OS_FBCON_SCALE
#define CELL      (8 * SCALE)
#define COLS      (DISPLAY_WIDTH / CELL)
#define ROWS      (DISPLAY_HEIGHT / CELL)
#define POLL_MS   40u
#define BLINK_MS  500u
#define HISTORY   1500              /* how much already-printed output to show when starting */
#define COL_FG    RGB565(210, 255, 210)
#define COL_BG    COLOR_BLACK

/* Screen cells and colors live in external RAM, not scarce internal BSS. */
static struct { uint16_t cp[ROWS][COLS], fg[ROWS][COLS], bg[ROWS][COLS]; } *s_screen;
#define s_scr (s_screen->cp)
#define s_fg (s_screen->fg)
#define s_bg (s_screen->bg)
static bool fbcon_alloc_screen(void)
{
    if (!s_screen) s_screen = kmalloc_psram(sizeof *s_screen);
    return s_screen != NULL;
}
static uint16_t s_attr_fg = COL_FG, s_attr_bg = COL_BG;
static bool s_cursor_visible = true, s_alternate;
static bool s_dirty[ROWS];
static int  s_rows = ROWS;        /* rows in use: fbcon_set_rows() shrinks the console (on-screen keyboard below it) */
static int  s_row, s_col;
static volatile bool s_paused;
static bool s_cursor_on;
static int  s_cursor_row = -1, s_cursor_col;

/* Only the fbcon task mutates the history and viewport. Touch submits requests. */
#define SCROLLBACK_LINES 256
typedef struct {
    uint16_t text[COLS], fg[COLS], bg[COLS];
} history_line_t;
static history_line_t *s_history;
static int s_history_head, s_history_count, s_view_offset;
static volatile int s_scroll_request;
static volatile bool s_live_request;

static void dirty_all(void)
{
    for (int r = 0; r < s_rows; r++) s_dirty[r] = true;
    s_cursor_on = false;
    s_cursor_row = -1;
}

static void history_save(const uint16_t *line)
{
    if (!s_history || s_alternate) return;
    int row = (int)(((uintptr_t)line - (uintptr_t)s_scr) / sizeof(s_scr[0]));
    for (int c = 0; c < COLS; c++) {
        s_history[s_history_head].text[c] = line[c];
        s_history[s_history_head].fg[c] = s_fg[row][c];
        s_history[s_history_head].bg[c] = s_bg[row][c];
    }
    s_history_head = (s_history_head + 1) % SCROLLBACK_LINES;
    if (s_history_count < SCROLLBACK_LINES) s_history_count++;
    /* Keep the text under the finger stationary while new output arrives. */
    if (s_view_offset > 0) {
        if (s_view_offset < s_history_count) s_view_offset++;
        dirty_all();
    }
}

static void view_line(int row, const uint16_t **text, const uint16_t **fg,
                      const uint16_t **bg)
{
    int v = s_history_count - s_view_offset + row;
    if (v >= s_history_count) {
        int r = v - s_history_count;
        *text = s_scr[r]; *fg = s_fg[r]; *bg = s_bg[r];
        return;
    }
    int oldest = (s_history_head - s_history_count + SCROLLBACK_LINES) % SCROLLBACK_LINES;
    const history_line_t *line = &s_history[(oldest + v) % SCROLLBACK_LINES];
    *text = line->text; *fg = line->fg; *bg = line->bg;
}

static void apply_scroll_request(void)
{
    uint32_t irq = arch_irq_disable();
    int delta = s_scroll_request;
    bool live = s_live_request;
    s_scroll_request = 0;
    s_live_request = false;
    arch_irq_restore(irq);
    int before = s_view_offset;
    if (live || s_alternate) s_view_offset = 0;
    else {
        s_view_offset += delta;
        if (s_view_offset < 0) s_view_offset = 0;
        if (s_view_offset > s_history_count) s_view_offset = s_history_count;
    }
    if (s_view_offset != before) dirty_all();
}

static void clear_row(int r)
{
    for (int c = 0; c < COLS; c++) { s_scr[r][c] = ' '; s_fg[r][c] = s_attr_fg; s_bg[r][c] = s_attr_bg; }
    s_dirty[r] = true;
}

#include "fbcon_escape.inc"

void fbcon_overlay_colors(int style, uint16_t *fg, uint16_t *bg)
{
    switch (style) {
    case FBCON_OVL_SEGMENT:  *fg = RGB565(255, 255, 255); *bg = RGB565(40, 90, 170);  break;
    case FBCON_OVL_FOCUS:    *fg = RGB565(0, 0, 0);       *bg = RGB565(255, 215, 70); break;
    case FBCON_OVL_CAND:     *fg = RGB565(0, 0, 0);       *bg = RGB565(225, 228, 235); break;
    case FBCON_OVL_CAND_SEL: *fg = RGB565(255, 255, 255); *bg = RGB565(0, 100, 200);  break;
    case FBCON_OVL_BADGE:    *fg = RGB565(0, 0, 0);       *bg = RGB565(80, 220, 120); break;
    case FBCON_OVL_BAR:      *fg = RGB565(255, 255, 255); *bg = RGB565(45, 45, 60);   break;
    case FBCON_OVL_PENDING:  *fg = RGB565(255, 215, 70);  *bg = RGB565(20, 40, 110);  break;
    default:                 *fg = RGB565(255, 255, 255); *bg = RGB565(20, 40, 110);  break;
    }
}

#if M5OS_BOARD_TAB5        /* only the Tab5 has the Japanese input method; the other boards keep their RAM */
/* ---- overlay (see fbcon.h) --------------------------------------------------------------------- */

#define OVL_SPANS 24
#define OVL_CELLS 80

typedef struct {
    int16_t row, col;               /* anchor-relative (cursor) or absolute (screen) */
    uint8_t anchor, len, style;
    uint16_t cp[OVL_CELLS];
} ovl_span_t;

typedef struct { int16_t row, col; uint8_t len, style; uint16_t cp[OVL_CELLS]; } ovl_abs_t;

/* The spans take ~16 KB, so they live in PSRAM and are allocated when the first overlay is built. */
static struct ovl_mem {
    ovl_span_t stage[OVL_SPANS], live[OVL_SPANS];
    ovl_abs_t  paint[OVL_SPANS];                /* what is on the screen now */
    ovl_abs_t  now[OVL_SPANS];                  /* scratch for ovl_prepare */
} *s_ovl;
#define s_ovl_stage (s_ovl->stage)
#define s_ovl_live  (s_ovl->live)
#define s_ovl_paint (s_ovl->paint)
static int s_ovl_nstage, s_ovl_nlive, s_ovl_npaint;
static volatile uint32_t s_ovl_gen;
static uint32_t s_ovl_seen;

void fbcon_overlay_begin(void)
{
    if (!s_ovl) s_ovl = kmalloc_psram(sizeof *s_ovl);
    s_ovl_nstage = 0;
}

void fbcon_overlay_text(int anchor, int row, int col, const uint16_t *cps, int n, int style)
{
    if (!s_ovl || s_ovl_nstage >= OVL_SPANS || n <= 0) return;
    if (n > OVL_CELLS) n = OVL_CELLS;
    ovl_span_t *s = &s_ovl_stage[s_ovl_nstage++];
    s->anchor = (uint8_t)anchor; s->row = (int16_t)row; s->col = (int16_t)col; s->len = (uint8_t)n; s->style = (uint8_t)style;
    for (int i = 0; i < n; i++) s->cp[i] = cps[i];
}

void fbcon_overlay_end(void)
{
    if (!s_ovl) return;
    uint32_t irq = arch_irq_disable();
    s_ovl_nlive = s_ovl_nstage;
    for (int i = 0; i < s_ovl_nstage; i++) s_ovl_live[i] = s_ovl_stage[i];
    s_ovl_gen++;
    arch_irq_restore(irq);
}

void fbcon_get_geometry(int *rows, int *cols, int *cur_row, int *cur_col)
{
    if (rows) *rows = s_rows;
    if (cols) *cols = COLS;
    if (cur_row) *cur_row = s_row;
    if (cur_col) *cur_col = s_col < COLS ? s_col : COLS - 1;
}

static bool ovl_covers(int r, int c)
{
    if (!s_ovl) return false;
    for (int i = 0; i < s_ovl_npaint; i++)
        if (s_ovl_paint[i].row == r && c >= s_ovl_paint[i].col && c < s_ovl_paint[i].col + s_ovl_paint[i].len) return true;
    return false;
}

/* Works out where the live spans go on the screen now; marks the rows of the old ones for repainting when
 * anything moved.  Returns true if the overlay has to be painted again. */
static bool ovl_prepare(void)
{
    if (!s_ovl) return false;
    ovl_abs_t *now = s_ovl->now;
    int n = 0;
    bool changed = s_ovl_gen != s_ovl_seen;
    uint32_t irq = arch_irq_disable();
    s_ovl_seen = s_ovl_gen;
    for (int i = 0; i < s_ovl_nlive; i++) {
        const ovl_span_t *s = &s_ovl_live[i];
        int r = s->row, c = s->col;
        if (s->anchor == FBCON_ANCHOR_CURSOR) { r += s_row; c += (s_col < COLS ? s_col : COLS - 1); }
        else { if (r < 0) r += s_rows; if (c < 0) c += COLS; }
        int len = s->len;
        const uint16_t *cp = s->cp;
        if (c < 0) { cp -= c; len += c; c = 0; }
        if (c + len > COLS) len = COLS - c;
        if (r < 0 || r >= s_rows || len <= 0) continue;
        now[n].row = (int16_t)r; now[n].col = (int16_t)c; now[n].len = (uint8_t)len; now[n].style = s->style;
        for (int k = 0; k < len; k++) now[n].cp[k] = cp[k];
        n++;
    }
    arch_irq_restore(irq);
    if (n != s_ovl_npaint) changed = true;
    for (int i = 0; !changed && i < n; i++)
        if (now[i].row != s_ovl_paint[i].row || now[i].col != s_ovl_paint[i].col || now[i].len != s_ovl_paint[i].len) changed = true;
    if (!changed) return false;
    for (int i = 0; i < s_ovl_npaint; i++) s_dirty[s_ovl_paint[i].row] = true;          /* put the console text back */
    s_cursor_on = false;
    for (int i = 0; i < n; i++) s_ovl_paint[i] = now[i];
    s_ovl_npaint = n;
    return true;
}

static void ovl_paint(void)
{
    if (!s_ovl) return;
    for (int i = 0; i < s_ovl_npaint; i++) {
        uint16_t fg, bg;
        fbcon_overlay_colors(s_ovl_paint[i].style, &fg, &bg);
        display_draw_codepoints(s_ovl_paint[i].col * CELL, s_ovl_paint[i].row * CELL, s_ovl_paint[i].cp, s_ovl_paint[i].len, fg, bg, SCALE);
    }
}

#else
void fbcon_overlay_begin(void) {}
void fbcon_overlay_text(int anchor, int row, int col, const uint16_t *cps, int n, int style) { (void)anchor; (void)row; (void)col; (void)cps; (void)n; (void)style; }
void fbcon_overlay_end(void) {}
void fbcon_get_geometry(int *rows, int *cols, int *cur_row, int *cur_col)
{
    if (rows) *rows = s_rows;
    if (cols) *cols = COLS;
    if (cur_row) *cur_row = s_row;
    if (cur_col) *cur_col = s_col < COLS ? s_col : COLS - 1;
}
static inline bool ovl_covers(int r, int c) { (void)r; (void)c; return false; }
static inline bool ovl_prepare(void) { return false; }
static inline void ovl_paint(void) {}
#endif

static void draw_cursor(bool on)
{
    on = on && s_cursor_visible;
    int r = s_row, c = s_col < COLS ? s_col : COLS - 1;
    if (ovl_covers(r, c)) { s_cursor_row = r; s_cursor_col = c; s_cursor_on = false; return; }      /* the overlay is there instead */
    uint16_t under = s_scr[r][c];
    display_draw_codepoints(c * CELL, r * CELL, &under, 1, on ? s_bg[r][c] : s_fg[r][c], on ? s_fg[r][c] : s_bg[r][c], SCALE);
    s_cursor_row = r; s_cursor_col = c; s_cursor_on = on;
}

static void flush_rows(void)
{
    for (int r = 0; r < s_rows; r++) {
        if (!s_dirty[r]) continue;
        const uint16_t *text, *fg, *bg;
        view_line(r, &text, &fg, &bg);
        for (int c = 0; c < COLS;) {
            int end = c + 1;
            while (end < COLS && fg[end] == fg[c] && bg[end] == bg[c]) end++;
            display_draw_codepoints(c * CELL, r * CELL, text + c, end - c,
                                    fg[c], bg[c], SCALE);
            c = end;
        }
        s_dirty[r] = false;
        if (r == s_cursor_row) s_cursor_on = false;         /* the row was repainted: the cursor cell is gone */
    }
}

static void fbcon_task(void *arg)
{
    (void)arg;
#if M5OS_BOARD_TAB5
    s_history = kmalloc_psram(sizeof(*s_history) * SCROLLBACK_LINES);
#endif
    for (int r = 0; r < s_rows; r++) clear_row(r);
    uint32_t end = kconsole_log_end();
    uint32_t pos = end > HISTORY ? end - HISTORY : 0;
    uint32_t last_blink = (uint32_t)(hal_time_now_us() / 1000u);
    bool blink = true;

    for (;;) {
        char buf[64];
        size_t n;
        bool moved = false;
        while ((n = kconsole_log_read(&pos, buf, sizeof buf)) > 0) { for (size_t i = 0; i < n; i++) feed(buf[i]); moved = true; }
        if (s_paused) { task_sleep(POLL_MS); continue; }
        apply_scroll_request();
        if (s_view_offset > 0 && moved) dirty_all();
        if (s_view_offset == 0) ovl_prepare();
        flush_rows();
        if (s_view_offset > 0) {
            task_sleep(POLL_MS);
            continue; /* No live cursor or input-method overlay on historical text. */
        }

        uint32_t now = (uint32_t)(hal_time_now_us() / 1000u);
        if (moved) { blink = true; last_blink = now; }
        else if (now - last_blink >= BLINK_MS) { blink = !blink; last_blink = now; moved = true; }

        int cur_c = s_col < COLS ? s_col : COLS - 1;
        if (moved || s_cursor_row != s_row || s_cursor_col != cur_c) {
            if (s_cursor_row >= 0 && (s_cursor_row != s_row || s_cursor_col != cur_c) && s_cursor_on && !ovl_covers(s_cursor_row, s_cursor_col)) {
                uint16_t under = s_scr[s_cursor_row][s_cursor_col];
                display_draw_codepoints(s_cursor_col * CELL, s_cursor_row * CELL, &under, 1, s_fg[s_cursor_row][s_cursor_col], s_bg[s_cursor_row][s_cursor_col], SCALE);
            }
            draw_cursor(blink);
        }
        ovl_paint();                                            /* on top of everything else, every pass: it is small */
        task_sleep(POLL_MS);
    }
}

/* Positive rows reveal older output. Requests are bounded to avoid overflow. */
void fbcon_scroll(int rows)
{
    uint32_t irq = arch_irq_disable();
    if (rows > SCROLLBACK_LINES) rows = SCROLLBACK_LINES;
    if (rows < -SCROLLBACK_LINES) rows = -SCROLLBACK_LINES;
    s_scroll_request += rows;
    if (s_scroll_request > SCROLLBACK_LINES) s_scroll_request = SCROLLBACK_LINES;
    if (s_scroll_request < -SCROLLBACK_LINES) s_scroll_request = -SCROLLBACK_LINES;
    arch_irq_restore(irq);
}

void fbcon_scroll_live(void)
{
    uint32_t irq = arch_irq_disable();
    s_scroll_request = 0;
    s_live_request = true;
    arch_irq_restore(irq);
}

bool fbcon_is_paused(void) { return s_paused; }

int fbcon_cell_px(void) { return CELL; }
int fbcon_max_rows(void) { return ROWS; }

void fbcon_set_rows(int rows)
{
    if (rows < 4) rows = 4;
    if (rows > ROWS) rows = ROWS;
    uint32_t irq = arch_irq_disable();
    if (rows == s_rows) { arch_irq_restore(irq); return; }
    if (!s_screen) { s_rows=rows; s_row=0; s_col=0; arch_irq_restore(irq); return; }
    if (s_row >= rows) {                                        /* keep the cursor line visible: scroll up */
        int d = s_row - (rows - 1);
        for (int r = 0; r < d; r++) history_save(s_scr[r]);
        for (int r = 0; r < rows; r++)
            for (int c = 0; c < COLS; c++) {
                s_scr[r][c] = s_scr[r + d][c];
                s_fg[r][c] = s_fg[r + d][c];
                s_bg[r][c] = s_bg[r + d][c];
            }
        s_row = rows - 1;
    }
    for (int r = rows; r < ROWS; r++) clear_row(r);
    for (int r = 0; r < rows; r++) s_dirty[r] = true;
    s_cursor_row = -1;
    s_rows = rows;
    s_margin_top=0; s_margin_bottom=-1; s_origin=false;
    arch_irq_restore(irq);
}

void fbcon_pause(bool pause)
{
    if (!pause) {
        display_clear(COL_BG);
        for (int r = 0; r < s_rows; r++) s_dirty[r] = true;      /* repaint everything on the next pass */
        s_cursor_row = -1;
    }
    s_paused = pause;
}

void fbcon_start(void)
{
    if (!fbcon_alloc_screen()) { kprintf("fbcon: no external RAM for screen\n"); return; }
    task_create_stack("fbcon", fbcon_task, NULL, TASK_PRIO_DEFAULT + 1, 4096);
}
