/* Host-only regression checks for LCD viewport/history; no device access. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../drivers/lcd/fbcon.c"

void kprintf(const char *fmt, ...) { (void)fmt; }
uint32_t arch_irq_disable(void) { return 0; }
void arch_irq_restore(uint32_t flags) { (void)flags; }
static bool fail_alloc;
void *kmalloc_psram(size_t size) { return fail_alloc ? NULL : calloc(1, size); }
void kfree(void *p) { free(p); }
void display_draw_codepoints(int x, int y, const uint16_t *cps, int n,
                             uint16_t fg, uint16_t bg, int scale)
{ (void)x; (void)y; (void)cps; (void)n; (void)fg; (void)bg; (void)scale; }
bool display_begin_frame(void) { return false; }
bool display_end_frame(void) { return false; }
void display_clear(uint16_t color) { (void)color; }
uint64_t hal_time_now_us(void) { return 0; }
void task_sleep(uint32_t ms) { (void)ms; }
int task_create_stack(const char *name, void (*entry)(void *), void *arg,
                      uint8_t priority, size_t size)
{ (void)name; (void)entry; (void)arg; (void)priority; (void)size; return 1; }
uint32_t kconsole_log_end(void) { return 0; }
size_t kconsole_log_read(uint32_t *pos, char *buf, size_t max)
{ (void)pos; (void)buf; (void)max; return 0; }
void kconsole_inject(const char *s) { (void)s; }
size_t kconsole_inject_n(const char *s, size_t n) { (void)s; return n; }

static void save_number(int n)
{
    s_scr[0][0] = (uint16_t)n;
    s_fg[0][0] = (uint16_t)(n + 1000);
    s_bg[0][0] = (uint16_t)(n + 2000);
    history_save(s_scr[0]);
}
static void check_line(int row, int expected)
{
    const uint16_t *text, *fg, *bg;
    view_line(row, &text, &fg, &bg);
    assert(text[0] == expected);
    assert(fg[0] == expected + 1000);
    assert(bg[0] == expected + 2000);
}
int main(void)
{
    fail_alloc=true;
    assert(!fbcon_alloc_screen());
    fail_alloc=false;
    assert(fbcon_alloc_screen());
    s_history = kmalloc_psram(sizeof(*s_history) * SCROLLBACK_LINES);
    assert(s_history);
    for (int i = 0; i < 300; i++) save_number(i);
    assert(s_history_count == 256);
    fbcon_scroll(10000); apply_scroll_request();
    assert(s_view_offset == 256);
    check_line(0, 44); check_line(3, 47);
    fbcon_scroll(-246); apply_scroll_request();
    assert(s_view_offset == 10); check_line(0, 290);
    save_number(300);
    assert(s_view_offset == 11); check_line(0, 290);
    fbcon_scroll_live(); apply_scroll_request();
    assert(s_view_offset == 0); check_line(0, 300);
    fbcon_scroll(-10000); apply_scroll_request(); assert(s_view_offset == 0);
    fbcon_scroll(100); fbcon_scroll_live(); apply_scroll_request();
    assert(s_view_offset == 0);
    int count = s_history_count, head = s_history_head;
    s_alternate = true; save_number(400); s_alternate = false;
    assert(s_history_count == count && s_history_head == head);
    s_rows = 8; s_row = 7;
    for (int r = 0; r < s_rows; r++) {
        s_scr[r][0] = (uint16_t)(500 + r);
        s_fg[r][0] = (uint16_t)(1500 + r);
        s_bg[r][0] = (uint16_t)(2500 + r);
    }
    fbcon_set_rows(4);
    assert(s_row == 3 && s_rows == 4); check_line(0, 504);
    fbcon_scroll(4); apply_scroll_request();
    check_line(0, 500); check_line(3, 503);
    free(s_history); s_history = NULL;
    s_history_count = s_history_head = s_view_offset = 0;
    save_number(900); fbcon_scroll(99); apply_scroll_request();
    assert(s_history_count == 0 && s_view_offset == 0);
    puts("PASS: history wrap, colors, clamps, output anchoring, alternate isolation, resize, allocation failure");
    return 0;
}
