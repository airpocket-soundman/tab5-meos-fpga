
#include "sc202cs_tab5_init.h"
#include <m5os/camera.h>
#include <m5os/kprintf.h>
#include <m5os/mutex.h>
#include <m5os/task.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FB_BYTES (CAMERA_WIDTH * CAMERA_HEIGHT * 2u)
#define CACHE_ALIGN 128u
#define ESP_CACHE_MSYNC_FLAG_DIR_C2M 1
#define ESP_CACHE_MSYNC_FLAG_DIR_M2C 2
static bool s_inited, s_streaming, s_poisoned;
static uint16_t s_chip_id, *s_fb[2];
static void *s_fb_raw[2];
static const uint16_t *s_ready_fb;
static uint32_t s_frames, s_owner, s_dma_errors;
static int s_write_idx;
static char s_error[80];
static uint32_t s_exposure = SC202CS_EXP_DEFAULT, s_gain_idx = 8;
static mutex_t s_camera_lock = MUTEX_INIT;
static int s_isp_context, *s_isp;
static task_t current = {.id = 2};
static unsigned live_allocs, sensor_writes;
static bool clock_on, hw_on, halt_ok = true;
static int allocate_fail = -1, poll_result;
static unsigned hardware_id = SC202CS_CHIP_ID;
task_t *task_current(void) { return &current; }
void mutex_lock(mutex_t *m) {}
void mutex_unlock(mutex_t *m) {}
void *kmalloc_psram(size_t n) {
    if (allocate_fail == 0)
        return NULL;
    if (allocate_fail > 0)
        allocate_fail--;
    void *p = malloc(n);
    if (p)
        live_allocs++;
    return p;
}
void kfree(void *p) {
    if (p)
        live_allocs--;
    free(p);
}
void kprintf(const char *s, ...) { (void)s; }
int ksnprintf(char *b, size_t n, const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    int r = vsnprintf(b, n, f, ap);
    va_end(ap);
    return r;
}
static bool xclk_start(void) {
    clock_on = true;
    return true;
}
static void xclk_stop(void) { clock_on = false; }
static void delay_ms(unsigned n) {}
static bool sccb_read(uint16_t reg, uint8_t *v) {
    *v = reg == SC202CS_REG_CHIP_H ? hardware_id >> 8 : hardware_id;
    return true;
}
static bool sccb_write(uint16_t reg, uint8_t value) {
    sensor_writes++;
    return true;
}
static bool sensor_init_regs(void) {
    sensor_writes++;
    return true;
}
static bool native_camera_init(void) {
    hw_on = true;
    return true;
}
static bool native_camera_start(void *p) { return true; }
static bool native_camera_stop(void) { return halt_ok; }
static void native_camera_deinit(void) { hw_on = false; }
static int native_camera_poll(void) {
    int n = poll_result;
    poll_result = 0;
    return n;
}
static void configure_isp_pipeline(int *isp) {}
static int esp_cache_msync(void *p, size_t n, unsigned flags) { return 0; }
bool camera_set_exposure(uint32_t n) { return true; }
bool camera_set_gain_index(uint32_t n) { return true; }
/* CAMERA_LIFECYCLE */
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            printf("FAIL line %d %s\n", __LINE__, #x);                                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main(void) {
    CHECK(camera_probe());
    CHECK(!clock_on && live_allocs == 0 && sensor_writes == 0);
    hardware_id = 0x1234;
    CHECK(!camera_start());
    CHECK(sensor_writes == 0 && !clock_on && live_allocs == 0);
    hardware_id = SC202CS_CHIP_ID;
    allocate_fail = 1;
    CHECK(!camera_init());
    CHECK(live_allocs == 0 && !clock_on);
    allocate_fail = -1;
    CHECK(camera_start());
    CHECK(s_streaming && s_owner == 2 && live_allocs == 2);
    uint32_t seq = 777;
    CHECK(!camera_frame(&seq) && seq == 0);
    poll_result = 1;
    CHECK(camera_frame(&seq) && seq == 1);
    current.id = 3;
    CHECK(!camera_start());
    camera_stop();
    CHECK(s_streaming && s_owner == 2);
    CHECK(!camera_frame(&seq) && seq == 0);
    camera_deinit();
    CHECK(live_allocs == 2);
    current.id = 2;
    camera_stop();
    CHECK(!s_streaming);
    CHECK(camera_start());
    CHECK(!camera_frame(&seq) && seq == 0);
    camera_deinit();
    CHECK(!s_inited && !clock_on && !hw_on && live_allocs == 0);
    CHECK(camera_start());
    halt_ok = false;
    camera_stop();
    camera_deinit();
    CHECK(s_poisoned && live_allocs == 2);
    CHECK(!camera_start());
    puts("PASS camera lifecycle readonly probe, unknown ID rejection, failed init cleanup, owner "
         "exclusion, fresh restart, DMA memory retention on halt failure");
    return 0;
}
