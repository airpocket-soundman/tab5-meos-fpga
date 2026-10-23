/* Actual display transaction include, mocked DPI scanout and MeOS scheduling. */
#include <assert.h>
#include <m5os/display.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PANEL_W 4
#define PANEL_H 8
#undef DISPLAY_BUFFER_BYTES
#define DISPLAY_BUFFER_BYTES (PANEL_W * PANEL_H * sizeof(uint16_t))
#define TASK_MAX 16
typedef struct {
    uint32_t id;
} task_t;
typedef struct {
    task_t *owner;
    void *unused;
} mutex_t;
#define MUTEX_INIT {0, 0}
typedef int esp_err_t;
#define ESP_OK 0
static task_t main_task = {1}, other_task = {2};
static task_t *current = &main_task;
static uint16_t buffers[2][PANEL_W * PANEL_H], external[PANEL_W * PANEL_H];
static uint16_t *s_fb = buffers[0], *s_back = buffers[1], *s_draw = buffers[0],
                *scanout = buffers[0], *pending;
static void *s_dpi_panel = (void *)1;
static volatile uint32_t s_frames;
static uint8_t s_fb_count = 2;
static uintptr_t display_scanout_address(void) { return (uintptr_t)scanout; }
static uint64_t now_us;
static bool stalled, queue_error, source_stalled;
static unsigned locks, unlocks, sleeps, presents;
static jmp_buf killed;
static bool die_on_submit, die_on_sleep;
static task_t *task_current(void) { return current; }
static uint32_t arch_irq_disable(void) { return 0; }
static void arch_irq_restore(uint32_t flags) { (void)flags; }
static void mutex_lock(mutex_t *m) {
    assert(!m->owner);
    m->owner = current;
    locks++;
}
static void mutex_unlock(mutex_t *m) {
    assert(m->owner == current);
    m->owner = NULL;
    unlocks++;
}
static uint64_t hal_time_now_us(void) { return now_us; }
static void kprintf(const char *fmt, ...) { (void)fmt; }
static void task_sleep(uint32_t ms) {
    if (die_on_sleep)
        longjmp(killed, 1);
    now_us += (uint64_t)ms * 1000;
    sleeps++;
    if (!stalled) {
        if (pending && !source_stalled)
            scanout = pending;
        s_frames++;
    }
    /* Waiting tasks must not be handed the preceding DMA buffer early. */
    assert(s_back == pending);
}
static esp_err_t esp_lcd_panel_draw_bitmap(void *panel, int x, int y, int w, int h,
                                           const void *data) {
    if (die_on_submit)
        longjmp(killed, 1);
    assert(panel && x == 0 && y == 0 && w == PANEL_W && h == PANEL_H);
    presents++;
    if (queue_error)
        return -1;
    pending = (uint16_t *)data;
    return ESP_OK;
}
void display_fill_rect(int, int, int, int, uint16_t);
static void display_fill_rect_unlocked(int x, int y, int w, int h, uint16_t c) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    s_draw[0] = c;
}
static void display_clear_unlocked(uint16_t c) {
    for (unsigned i = 0; i < PANEL_W * PANEL_H; i++)
        s_draw[i] = c;
}
static void display_draw_rect_unlocked(int x, int y, int w, int h, uint16_t c) {
    display_fill_rect(x, y, w, h, c);
}
static void display_draw_char_unlocked(int x, int y, char c, uint16_t f, uint16_t b, int n) {
    (void)x;
    (void)y;
    (void)c;
    (void)f;
    (void)b;
    (void)n;
}
static void display_draw_codepoints_unlocked(int x, int y, const uint16_t *c, int n, uint16_t f,
                                             uint16_t b, int z) {
    (void)x;
    (void)y;
    (void)c;
    (void)n;
    (void)f;
    (void)b;
    (void)z;
}
static void display_draw_text_unlocked(int x, int y, const char *c, uint16_t f, uint16_t b, int z) {
    (void)x;
    (void)y;
    (void)c;
    (void)f;
    (void)b;
    (void)z;
}
static void display_draw_bitmap_unlocked(int x, int y, int w, int h, const uint16_t *c) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)c;
}
static void display_draw_line_unlocked(int a, int b, int c, int d, uint16_t f) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)f;
}
static void display_fill_triangle_unlocked(int a, int b, int c, int d, int e, int f, uint16_t g) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    (void)g;
}
static void display_set_target_unlocked(uint16_t *p) { s_draw = p ? p : s_fb; }
static void display_present_unlocked(int a, int b, int c, int d) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
}
static void display_read_bitmap_unlocked(int a, int b, int c, int d, uint16_t *e) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
}
static void display_draw_bitmap_scaled_unlocked(int a, int b, int c, int d, const uint16_t *e,
                                                int f) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
}
#include "../../drivers/lcd/display_frame.inc"
int main(void) {
    for (unsigned i = 0; i < PANEL_W * PANEL_H; i++)
        s_fb[i] = (uint16_t)i;
    assert(display_begin_frame());
    assert(s_display_mutex.owner == current);
    display_status_t status;
    display_get_status(&status); /* Must not recursively acquire the render mutex. */
    assert(status.batches == 1 && status.framebuffer_count == 2 && status.batch_active);
    assert(status.back_available && !status.pending && status.retired == 0);
    assert(s_draw == buffers[1] && memcmp(buffers[0], buffers[1], DISPLAY_BUFFER_BYTES) == 0);
    assert(!display_begin_frame()); /* A nested batch cannot release its outer lock. */
    unsigned before = locks;
    display_draw_rect(0, 0, 1, 1, 999);
    assert(locks == before && s_fb[0] == 0 && s_draw[0] == 999 && scanout[0] == 0);
    assert(display_end_frame());
    assert(presents == 1 && sleeps == 1);
    assert(s_fb == buffers[1] && s_back == buffers[0] && scanout == s_fb && s_draw == s_fb);
    assert(s_fb[0] == 999 && s_fb[7] == 7 && s_display_mutex.owner == NULL);
    display_get_status(&status);
    assert(status.submitted == 1 && status.retired == 1 && status.refreshes == 1);
    assert(status.last_wait_us == 1000 && status.last_batch_us == 1000 && !status.batch_active);
    assert(locks == unlocks);
    assert(display_begin_frame());
    s_draw[1] = 1234;
    queue_error = true;
    assert(!display_end_frame());
    assert(s_fb == buffers[1] && s_fb[1] == 1 && s_draw == s_fb);
    queue_error = false;
    assert(display_begin_frame());
    s_draw[2] = 4321;
    assert(display_end_frame());
    assert(s_fb[2] == 4321 && s_fb[1] == 1);
    display_set_target(external);
    assert(!display_begin_frame());
    current = &other_task;
    display_fill_rect(0, 0, 1, 1, 909);
    assert(s_fb[0] == 909 && external[0] == 0);
    current = &main_task;
    display_fill_rect(0, 0, 1, 1, 808);
    assert(external[0] == 808 && s_fb[0] == 909);
    main_task.id = 3; /* The same TCB address is reused after an app is killed. */
    display_fill_rect(0, 0, 1, 1, 707);
    assert(external[0] == 808 && s_fb[0] == 707);
    display_set_target(NULL);
    current = NULL;
    assert(!display_begin_frame());
    current = &main_task;
    assert(display_begin_frame());
    current = &other_task;
    assert(!display_end_frame());
    current = &main_task;
    source_stalled = true; /* VSYNC keeps advancing while DMA source fails to switch. */
    uint32_t refresh_before = s_frames;
    assert(display_end_frame());
    assert(s_frames > refresh_before + 2);
    source_stalled = false;
    assert(s_back == NULL && s_display_mutex.owner == NULL);
    display_get_status(&status);
    assert(status.timeouts == 1 && status.queue_errors == 1 && !status.back_available);
    assert(status.framebuffer_count == 2 && status.max_batch_us >= 100000 && !status.pending);
    assert(!display_begin_frame()); /* Old, unretired scanout buffer cannot be reused. */
    assert(locks == unlocks);
    /* Simulate task_kill reclaiming the mutex immediately before the queue,
     * and separately after queueing while waiting for frame retirement. */
    for (int during_wait = 0; during_wait < 2; during_wait++) {
        s_back = s_fb == buffers[0] ? buffers[1] : buffers[0];
        scanout = s_fb;
        pending = NULL;
        stalled = false;
        assert(display_begin_frame());
        uint16_t *complete = s_back;
        complete[3] = 555;
        die_on_submit = !during_wait;
        die_on_sleep = during_wait;
        if (setjmp(killed) == 0) {
            display_end_frame();
            assert(false);
        }
        die_on_submit = die_on_sleep = false;
        assert(s_frame_pending == complete);
        s_display_mutex.owner = NULL;
        unlocks++; /* Kernel mutex reclamation. */
        current = &other_task;
        display_fill_rect(0, 0, 1, 1, 777);
        assert(s_fb == complete && pending == complete && s_back == NULL);
        assert(s_draw == s_fb && s_fb[0] == 777 && s_fb[3] == 555);
        assert(!s_frame_active && !s_frame_pending && locks == unlocks);
        current = &main_task;
    }
    display_get_status(&status);
    display_get_status(NULL);
    assert(status.orphaned == 2 && !status.batch_active && !status.pending);
    puts("PASS: atomic frame publication, old buffer retirement, preserved pixels, nested locks, "
         "queue failure, timeout, external target, killed owner recovery");
}
