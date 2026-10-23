/* Tab5 rendering transactions. The back buffer is one of esp_lcd DPI's own
 * framebuffers, so draw_bitmap selects it without CPU-copy/FreeRTOS semaphore
 * paths. DMA changes buffers only when the preceding frame finishes.
 * Every drawing entry point shares this MeOS mutex; a console batch cannot
 * accidentally capture a simultaneous OSK draw in its temporary target.
 */
static mutex_t s_display_mutex = MUTEX_INIT;
static task_t *s_frame_owner;
static bool s_frame_active;
static uint16_t *s_frame_pending;
static display_status_t s_display_stats;
static uint64_t s_frame_begin_us;
/* One slot per TCB address; the monotonically increasing ID defeats TCB reuse. */
static struct { task_t *task; uint32_t id; uint16_t *buffer; } s_targets[TASK_MAX];
static uint16_t *display_task_target(task_t *self) {
    if (self) for (unsigned i = 0; i < TASK_MAX; i++)
        if (s_targets[i].task == self && s_targets[i].id == self->id)
            return s_targets[i].buffer;
    return NULL;
}
static void display_save_target(task_t *self, uint16_t *buffer) {
    if (!self) return;
    for (unsigned i = 0; i < TASK_MAX; i++) {
        if (s_targets[i].task == self || !s_targets[i].task) {
            s_targets[i].task = self;
            s_targets[i].id = self->id;
            s_targets[i].buffer = buffer;
            return;
        }
    }
}


static bool display_enter(void) {
    task_t *self = task_current();
    if (!self || s_display_mutex.owner == self)
        return false; /* Boot or nested draw. */
    mutex_lock(&s_display_mutex);
    if (s_frame_active) {
        s_display_stats.orphaned++;
        /* A killed rendering task may have had its mutex reclaimed. Keep the
         * requested front buffer, but never reuse a possibly pending scanout. */
        if (s_frame_pending) {
            /* The owner could die immediately before or during queueing.
             * Re-select the complete buffer to make that state unambiguous. */
            if (esp_lcd_panel_draw_bitmap(s_dpi_panel, 0, 0, PANEL_W, PANEL_H, s_frame_pending) ==
                ESP_OK) {
                s_fb = s_frame_pending;
                s_display_stats.submitted++;
            } else {
                s_fb = NULL; /* Do not write either buffer with unknown ownership. */
                s_display_stats.queue_errors++;
            }
        }
        s_frame_pending = NULL;
        s_frame_active = false;
        s_frame_owner = NULL;
        s_back = NULL;
        s_draw = s_fb;
    }
    uint16_t *target = display_task_target(self);
    s_draw = target ? target : s_fb;
    return true;
}
static void display_leave(bool locked) {
    if (locked) {
        s_draw = s_fb;
        mutex_unlock(&s_display_mutex);
    }
}

bool display_begin_frame(void) {
    bool locked = display_enter();
    if (!locked || !s_fb || !s_back || !s_dpi_panel || s_draw != s_fb || s_frame_active) {
        display_leave(locked);
        return false;
    }
    /* Copy preserves the keyboard area and any pixels outside this update.
     * Only batches with multiple dirty console rows take this larger path. */
    s_frame_begin_us = hal_time_now_us();
    memcpy(s_back, s_fb, DISPLAY_BUFFER_BYTES);
    s_display_stats.last_copy_us = (uint32_t)(hal_time_now_us() - s_frame_begin_us);
    s_display_stats.last_wait_us = 0;
    s_display_stats.batches++;
    s_draw = s_back;
    s_frame_active = true;
    s_frame_owner = task_current();
    return true; /* The task owns the display mutex until end_frame. */
}

bool display_end_frame(void) {
    if (!s_frame_active || s_frame_owner != task_current() ||
        s_display_mutex.owner != task_current())
        return false;
    uint16_t *old_front = s_fb, *new_front = s_back;
    s_frame_pending = new_front;
    esp_err_t err = esp_lcd_panel_draw_bitmap(s_dpi_panel, 0, 0, PANEL_W, PANEL_H, new_front);
    if (err == ESP_OK) {
        s_display_stats.submitted++;
        /* A VSYNC event alone does not prove the DMA has switched sources.
         * Observe the actual channel SAR in the newly selected framebuffer.
         * Its linked list contains only that framebuffer, so the old buffer
         * cannot be read again after this observation. */
        uint64_t start = hal_time_now_us();
        bool retired = false;
        while (hal_time_now_us() - start < 100000u) {
            uintptr_t source = display_scanout_address();
            uintptr_t base = (uintptr_t)new_front;
            if (source >= base && source - base < DISPLAY_BUFFER_BYTES) {
                retired = true;
                break;
            }
            task_sleep(1);
        }
        s_display_stats.last_wait_us = (uint32_t)(hal_time_now_us() - start);
        s_fb = new_front;
        if (retired) {
            s_back = old_front;
            s_display_stats.retired++;
        } else {
            s_display_stats.timeouts++;
            /* Preserve both allocations; never draw into an unretired buffer.
             * Fall back to the legacy direct-render path on a stalled panel. */
            s_back = NULL;
            kprintf("display: frame completion timed out, buffered updates disabled\n");
        }
    } else {
        s_display_stats.queue_errors++;
        /* Driver rejected the queue; the previous front remains valid. */
        s_fb = old_front;
    }
    s_draw = s_fb;
    s_frame_active = false;
    s_frame_owner = NULL;
    s_frame_pending = NULL;
    s_display_stats.last_batch_us = (uint32_t)(hal_time_now_us() - s_frame_begin_us);
    if (s_display_stats.last_batch_us > s_display_stats.max_batch_us)
        s_display_stats.max_batch_us = s_display_stats.last_batch_us;
    mutex_unlock(&s_display_mutex);
    return err == ESP_OK;
}

void display_get_status(display_status_t *out) {
    if (!out)
        return;
    uint32_t irq = arch_irq_disable();
    *out = s_display_stats;
    out->refreshes = s_frames;
    out->scanout_address = (uint32_t)display_scanout_address();
    out->framebuffer_count = s_fb_count;
    out->back_available = s_back != NULL;
    out->batch_active = s_frame_active;
    out->pending = s_frame_pending != NULL;
    uint64_t begun = s_frame_begin_us;
    arch_irq_restore(irq);
    out->active_us = out->batch_active ? (uint32_t)(hal_time_now_us() - begun) : 0;
}

void display_fill_rect(int x, int y, int w, int h, uint16_t color) {
    bool held = display_enter();
    display_fill_rect_unlocked(x, y, w, h, color);
    display_leave(held);
}
void display_clear(uint16_t color) {
    bool held = display_enter();
    display_clear_unlocked(color);
    display_leave(held);
}
void display_draw_rect(int x, int y, int w, int h, uint16_t color) {
    bool held = display_enter();
    display_draw_rect_unlocked(x, y, w, h, color);
    display_leave(held);
}
void display_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, int scale) {
    bool held = display_enter();
    display_draw_char_unlocked(x, y, c, fg, bg, scale);
    display_leave(held);
}
void display_draw_codepoints(int x, int y, const uint16_t *cps, int count, uint16_t fg, uint16_t bg,
                             int scale) {
    bool held = display_enter();
    display_draw_codepoints_unlocked(x, y, cps, count, fg, bg, scale);
    display_leave(held);
}
void display_draw_text(int x, int y, const char *s, uint16_t fg, uint16_t bg, int scale) {
    bool held = display_enter();
    display_draw_text_unlocked(x, y, s, fg, bg, scale);
    display_leave(held);
}
void display_draw_bitmap(int x, int y, int w, int h, const uint16_t *pixels) {
    bool held = display_enter();
    display_draw_bitmap_unlocked(x, y, w, h, pixels);
    display_leave(held);
}
void display_draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
    bool held = display_enter();
    display_draw_line_unlocked(x0, y0, x1, y1, color);
    display_leave(held);
}
void display_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color) {
    bool held = display_enter();
    display_fill_triangle_unlocked(x0, y0, x1, y1, x2, y2, color);
    display_leave(held);
}
void display_set_target(uint16_t *buffer) {
    bool held = display_enter();
    if (!s_frame_active) {
        display_save_target(task_current(), buffer);
        display_set_target_unlocked(buffer);
    }
    display_leave(held);
}
void display_present(int x, int y, int w, int h) {
    bool held = display_enter();
    display_present_unlocked(x, y, w, h);
    display_leave(held);
}
void display_read_bitmap(int x, int y, int w, int h, uint16_t *out) {
    bool held = display_enter();
    display_read_bitmap_unlocked(x, y, w, h, out);
    display_leave(held);
}
void display_draw_bitmap_scaled(int x, int y, int w, int h, const uint16_t *pixels, int scale) {
    bool held = display_enter();
    display_draw_bitmap_scaled_unlocked(x, y, w, h, pixels, scale);
    display_leave(held);
}
