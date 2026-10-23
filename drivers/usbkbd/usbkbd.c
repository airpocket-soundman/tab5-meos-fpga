/* TinyUSB boot HID input, serialized with MSC by the MeOS USB task. */
#include "usbkbd.h"
#include "../usbhost/tiny_host.h"
#include "../usbmsc/usbmsc.h"
#include "hid_keys.h"
#include "tusb.h"
#include <m5os/hal_time.h>
#include <m5os/ime.h>
#include <m5os/kprintf.h>
#include <m5os/task.h>
#include <string.h>
static keyboard_stats_t stats = {0, -1, -1, -1, 0};
static bool caps;
typedef struct {
    uint8_t dev, idx, prev[8], held;
    uint64_t pressed, repeated;
} keyboard_t;
static keyboard_t keyboards[4];
static void emit(uint8_t k, uint8_t mods) {
    char buf[8];
    hid_decode(k, mods, caps, buf);
    if (buf[0]) {
        ime_feed(buf);
        stats.last_char = (unsigned char)buf[0];
    }
}
const char *usbkbd_type(void) {
    for (int i = 0; i < 4; i++)
        if (keyboards[i].dev)
            return "usb-hid";
    return "none";
}
void usbkbd_stats(keyboard_stats_t *out) { *out = stats; }
static keyboard_t *find(uint8_t dev, uint8_t idx) {
    for (int i = 0; i < 4; i++)
        if (keyboards[i].dev == dev && keyboards[i].idx == idx)
            return &keyboards[i];
    return NULL;
}
void tuh_hid_mount_cb(uint8_t dev, uint8_t idx, uint8_t const *desc, uint16_t len) {
    (void)desc;
    (void)len;
    if (tuh_hid_interface_protocol(dev, idx) != HID_ITF_PROTOCOL_KEYBOARD)
        return;
    for (int i = 0; i < 4; i++)
        if (!keyboards[i].dev) {
            keyboards[i] = (keyboard_t){.dev = dev, .idx = idx};
            kprintf("usbkbd: keyboard ready dev=%u interface=%u\n", dev, idx);
            if (!tuh_hid_receive_report(dev, idx))
                stats.i2c_errors++;
            break;
        }
}
void tuh_hid_umount_cb(uint8_t dev, uint8_t idx) {
    keyboard_t *k = find(dev, idx);
    if (k) {
        memset(k, 0, sizeof *k);
        kprintf("usbkbd: keyboard gone dev=%u\n", dev);
    }
}
void tuh_hid_report_received_cb(uint8_t dev, uint8_t idx, uint8_t const *r, uint16_t len) {
    keyboard_t *k = find(dev, idx);
    if (!k)
        return;
    if (len >= 8) {
        uint64_t now = hal_time_now_us();
        for (int i = 2; i < 8; i++) {
            uint8_t key = r[i];
            if (key < HID_USAGE_FIRST)
                continue;
            bool old = false;
            for (int j = 2; j < 8; j++)
                if (k->prev[j] == key)
                    old = true;
            if (old)
                continue;
            stats.presses++;
            stats.last_x = key;
            if (key == HID_USAGE_CAPSLOCK) {
                caps = !caps;
                continue;
            }
            emit(key, r[0]);
            k->held = key;
            k->pressed = k->repeated = now;
        }
        bool held = false;
        for (int i = 2; i < 8; i++)
            if (k->held && r[i] == k->held)
                held = true;
        if (!held)
            k->held = 0;
        memcpy(k->prev, r, 8);
    } else
        stats.i2c_errors++;
    if (!tuh_hid_receive_report(dev, idx))
        stats.i2c_errors++;
}
static void usb_task(void *arg) {
    (void)arg;
    if (!meos_usb_init()) {
        kprintf("usb: TinyUSB initialization failed\n");
        return;
    }
    kprintf("usb: TinyUSB host, OS_NONE, polled DWC2 FIFO mode\n");
    for (;;) {
        meos_usb_lock();
        meos_usb_poll_locked();
        uint64_t now = hal_time_now_us();
        for (int i = 0; i < 4; i++) {
            keyboard_t *k = &keyboards[i];
            if (k->dev && k->held && now - k->pressed > 400000 && now - k->repeated > 50000) {
                emit(k->held, k->prev[0]);
                k->repeated = now;
            }
        }
        meos_usb_unlock();
        usbmsc_service();
        task_sleep(1);
    }
}
void usbkbd_start(void) {
    task_create_stack("usbhost", usb_task, NULL, TASK_PRIO_DEFAULT + 2, 8192);
}
