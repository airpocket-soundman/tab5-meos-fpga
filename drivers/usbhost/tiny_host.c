/* TinyUSB is serialized by a MeOS mutex, with OS_NONE and polled DWC2 interrupts. */
#include "tiny_host.h"
#include "host/hcd.h"
#include "tusb.h"
#include <m5os/hal_time.h>
#include <m5os/mutex.h>
#include <m5os/task.h>
static mutex_t usb_lock = MUTEX_INIT;
extern bool meos_usb_phy_init(void);
void meos_usb_lock(void) { mutex_lock(&usb_lock); }
void meos_usb_unlock(void) { mutex_unlock(&usb_lock); }
uint32_t tusb_time_millis_api(void) { return (uint32_t)(hal_time_now_us() / 1000); }
void tusb_time_delay_ms_api(uint32_t ms) { task_sleep(ms ? ms : 1); }
bool meos_usb_init(void) {
    if (!meos_usb_phy_init())
        return false;
    tusb_rhport_init_t init = {.role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_HIGH};
    return tusb_init(1, &init);
}
void meos_usb_poll_locked(void) {
    hcd_int_handler(1, false);
    tuh_task_ext(0, false);
}
