/* Real fbcon presentation path, recording each publication boundary. */
#include "../../drivers/lcd/fbcon.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned begin_calls, end_calls, draw_calls;
static bool frame_available = true, frame_active, queue_ok = true, must_frame;
static int last_y;
void kprintf(const char *fmt, ...) { (void)fmt; }
uint32_t arch_irq_disable(void) { return 0; }
void arch_irq_restore(uint32_t f) { (void)f; }
void *kmalloc_psram(size_t n) { return calloc(1, n); }
void kfree(void *p) { free(p); }
bool display_begin_frame(void) {
    begin_calls++;
    frame_active = frame_available;
    return frame_active;
}
bool display_end_frame(void) {
    assert(frame_active);
    frame_active = false;
    end_calls++;
    return queue_ok;
}
void display_draw_codepoints(int x, int y, const uint16_t *c, int n, uint16_t fg, uint16_t bg,
                             int z) {
    (void)x;
    (void)c;
    (void)n;
    (void)fg;
    (void)bg;
    (void)z;
    if (must_frame)
        assert(frame_active);
    last_y = y;
    draw_calls++;
}
void display_clear(uint16_t c) { (void)c; }
uint64_t hal_time_now_us(void) { return 1000; }
void task_sleep(uint32_t n) { (void)n; }
int task_create_stack(const char *n, void (*f)(void *), void *a, uint8_t p, size_t s) {
    (void)n;
    (void)f;
    (void)a;
    (void)p;
    (void)s;
    return 1;
}
uint32_t kconsole_log_end(void) { return 0; }
size_t kconsole_log_read(uint32_t *p, char *b, size_t n) {
    (void)p;
    (void)b;
    (void)n;
    return 0;
}
void kconsole_inject(const char *s) { (void)s; }
size_t kconsole_inject_n(const char *s, size_t n) {
    (void)s;
    return n;
}
int main(void) {
    assert(fbcon_alloc_screen());
    s_rows = 8;
    s_history = calloc(SCROLLBACK_LINES, sizeof *s_history);
    term_reset();
    s_view_offset = 1;
    s_history_count = 1;
    bool blink = true;
    uint32_t last = 0;
    must_frame = true;
    render_console(false, &blink, &last);
    assert(begin_calls == 1 && end_calls == 1 && draw_calls == 8 && last_y == 7 * CELL &&
           !frame_active);
    for (int r = 0; r < 8; r++)
        assert(!s_dirty[r]);
    /* No changed rows means no frame copy or redundant glyph painting. */
    unsigned before = draw_calls;
    render_console(false, &blink, &last);
    assert(draw_calls == before && begin_calls == 1);
    s_dirty[2] = true;
    must_frame = false;
    render_console(false, &blink, &last);
    assert(draw_calls == before + 1 && begin_calls == 1 && last_y == 2 * CELL);
    /* Mixed-color runs remain within one transaction, not separate flips. */
    s_fg[3][1] = 123;
    s_dirty[3] = s_dirty[4] = true;
    must_frame = true;
    before = draw_calls;
    render_console(false, &blink, &last);
    assert(begin_calls == 2 && end_calls == 2 && draw_calls >= before + 3 && !frame_active);
    /* Failed submission retains a retryable model; allocation fallback draws. */
    queue_ok = false;
    s_dirty[1] = s_dirty[2] = true;
    render_console(false, &blink, &last);
    for (int r = 0; r < 8; r++)
        assert(s_dirty[r]);
    frame_available = false;
    must_frame = false;
    queue_ok = true;
    unsigned ends = end_calls;
    render_console(false, &blink, &last);
    assert(end_calls == ends);
    for (int r = 0; r < 8; r++)
        assert(!s_dirty[r]);
    free(s_history);
    free(s_screen);
    puts("PASS: multi-row atomic batch, dirty-row minimization, color runs, submission retry and "
         "direct fallback");
}
