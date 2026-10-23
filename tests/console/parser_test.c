/* Host-only regression checks for LCD viewport/history; no device access. */
#include "../../drivers/lcd/fbcon.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void kprintf(const char *fmt, ...) { (void)fmt; }
uint32_t arch_irq_disable(void) { return 0; }
void arch_irq_restore(uint32_t flags) { (void)flags; }
static bool fail_alloc;
void *kmalloc_psram(size_t size) { return fail_alloc ? NULL : calloc(1, size); }
void kfree(void *p) { free(p); }
void display_draw_codepoints(int x, int y, const uint16_t *cps, int n, uint16_t fg, uint16_t bg,
                             int scale) {
    (void)x;
    (void)y;
    (void)cps;
    (void)n;
    (void)fg;
    (void)bg;
    (void)scale;
}
bool display_begin_frame(void) { return false; }
bool display_end_frame(void) { return false; }
void display_clear(uint16_t color) { (void)color; }
uint64_t hal_time_now_us(void) { return 0; }
void task_sleep(uint32_t ms) { (void)ms; }
int task_create_stack(const char *name, void (*entry)(void *), void *arg, uint8_t priority,
                      size_t size) {
    (void)name;
    (void)entry;
    (void)arg;
    (void)priority;
    (void)size;
    return 1;
}
uint32_t kconsole_log_end(void) { return 0; }
size_t kconsole_log_read(uint32_t *pos, char *buf, size_t max) {
    (void)pos;
    (void)buf;
    (void)max;
    return 0;
}
void kconsole_inject(const char *s) { (void)s; }
size_t kconsole_inject_n(const char *s, size_t n) {
    (void)s;
    return n;
}

static void send_text(const char *s) {
    while (*s)
        feed(*s++);
}
static void reset(void) {
    s_rows = 8;
    term_reset();
    s_history_count = s_history_head = s_view_offset = 0;
}
int main(void) {
    fail_alloc=true;
    assert(!fbcon_alloc_screen());
    fail_alloc=false;
    assert(fbcon_alloc_screen());
    s_history = kmalloc_psram(sizeof(*s_history) * SCROLLBACK_LINES);
    reset();
    send_text("\033[4;6H\033[2J");
    assert(s_row == 3 && s_col == 5);
    send_text("\033[31;44mR\033[38;5;196mG\033[48;2;10;20;30mB");
    assert(s_fg[3][5] == term_palette(1) && s_bg[3][5] == term_palette(4));
    assert(s_fg[3][6] == RGB565(255, 0, 0));
    assert(s_bg[3][7] == RGB565(10, 20, 30));
    send_text("\0337\033[1;1H\033[0m\0338");
    assert(s_row == 3 && s_col == 8 && s_attr_bg == RGB565(10, 20, 30));
    reset();
    for (int r = 0; r < 8; r++)
        s_scr[r][0] = 'A' + r;
    send_text("\033[3;6r\033[6;1H\033D");
    assert(s_scr[0][0] == 'A' && s_scr[2][0] == 'D' && s_scr[5][0] == ' ' && s_scr[6][0] == 'G');
    assert(s_history_count == 0);
    send_text("\033[3;1H\033M");
    assert(s_scr[2][0] == ' ' && s_scr[3][0] == 'D');
    send_text("\033[?6h\033[1;2H");
    assert(s_row == 2 && s_col == 1);
    send_text("\033[999A");
    assert(s_row == 2);
    send_text("\033[999B");
    assert(s_row == 5);
    reset();
    send_text("MAIN\033[?1049hALT");
    assert(s_alternate && s_scr[0][0] == 'A');
    send_text("\033[8;1H\n\n");
    assert(s_history_count == 0);
    send_text("\033[?1049l");
    assert(!s_alternate && s_scr[0][0] == 'M' && s_col == 4);
    send_text("\033[?25l");
    assert(!s_cursor_visible);
    send_text("\033[?25h");
    assert(s_cursor_visible);
    reset();
    send_text("abcde\033[1;3H\033[2@X");
    assert(s_scr[0][2] == 'X' && s_scr[0][4] == 'c');
    send_text("\033[1;3H\033[2P");
    assert(s_scr[0][2] == 'c');
    reset();
    send_text("\033(0lqk\033(B");
    assert(s_scr[0][0] == 0x250c && s_scr[0][1] == 0x2500 && s_scr[0][2] == 0x2510);
    reset();
    send_text("\033]bad\033Xhidden\033\\OK");
    assert(s_scr[0][0] == 'O' && s_scr[0][1] == 'K');
    send_text("\033Psecret\033\\!");
    assert(s_scr[0][2] == '!');
    reset();
    send_text("\033[?7l");
    for (int i = 0; i < COLS + 3; i++)
        feed('X');
    assert(s_row == 0 && s_col == COLS);
    send_text("\033[2J");
    assert(s_row == 0 && s_col == COLS - 1);
    reset();
    for (int i = 0; i < COLS; i++)
        feed('A');
    send_text("\033[31mB");
    assert(s_row == 1 && s_scr[1][0] == 'B');
    reset();
    send_text("\033[9999999999999999999999999999;1H");
    assert(s_row == 7);
    send_text("\033[2J\033[1;1H\xc0\x80");
    assert(s_scr[0][0] == 0xfffd);
    reset();
    uint32_t rng = 0x1739ac;
    for (int i = 0; i < 100000; i++) {
        rng = rng * 1664525u + 1013904223u;
        feed((char)(rng >> 24));
        assert(s_row >= 0 && s_row < s_rows && s_col >= 0 && s_col <= COLS);
        if (i % 1000 == 0) {
            feed(0x18);
            send_text("\033[7;8r");
            fbcon_set_rows(4);
            feed('\n');
            fbcon_set_rows(8);
        }
    }
    free(s_primary);
    free(s_history);
    free(s_screen);
    puts("PASS: native ANSI colors, margins, edits, save/restore, alternate, modes, strings, UTF-8 "
         "bounds");
    return 0;
}
