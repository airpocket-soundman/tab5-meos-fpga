/* MeOS block device using the TinyUSB MSC/BOT driver. Single directly attached LUN 0. */
#include "usbmsc.h"
#include "../../fs/fat/usb_fat.h"
#include "../usbhost/tiny_host.h"
#include "tusb.h"
#include <m5os/errno.h>
#include <m5os/hal_time.h>
#include <m5os/kprintf.h>
#include <m5os/task.h>
#include <string.h>
static uint8_t address;
static bool ready, mount_pending, detach_pending, done, ok;
static blockdev_t bd;
/* Keep asynchronous I/O away from caller memory, including after a timeout. */
static uint8_t io_buffer[4096] __attribute__((aligned(64)));
static bool complete(uint8_t dev, tuh_msc_complete_data_t const *r) {
    (void)dev;
    ok = r->csw && r->cbw && r->csw->signature == MSC_CSW_SIGNATURE && r->csw->tag == r->cbw->tag &&
         r->csw->status == 0 && r->csw->data_residue == 0;
    done = true;
    return true;
}
static int transfer(blockdev_t *b, uint32_t lba, uint32_t count, void *buf, bool write) {
    if (!buf || !count || lba >= b->block_count || count > b->block_count - lba)
        return ERR_INVAL;
    meos_usb_lock();
    int rc = ready ? 0 : ERR_NODEV;
    for (uint32_t i = 0; !rc && i < count;) {
        uint16_t n = count - i > 8 ? 8 : count - i;
        done = ok = false;
        if (write)
            memcpy(io_buffer, (uint8_t *)buf + i * 512, n * 512);
        bool submitted = write ? tuh_msc_write10(address, 0, io_buffer, lba + i, n, complete, 0)
                               : tuh_msc_read10(address, 0, io_buffer, lba + i, n, complete, 0);
        if (!submitted) {
            rc = ERR_IO;
            break;
        }
        uint64_t start = hal_time_now_us();
        while (!done && ready && hal_time_now_us() - start < 5000000) {
            meos_usb_poll_locked();
            if (!done)
                task_sleep(1);
        }
        if (!done || !ok) {
            rc = ERR_IO;
            ready = false;
            kprintf("usbmsc: transfer failed; reconnect device before reuse\n");
        }
        if (!rc && !write)
            memcpy((uint8_t *)buf + i * 512, io_buffer, n * 512);
        i += n;
    }
    meos_usb_unlock();
    return rc;
}
static int rd(blockdev_t *b, uint32_t l, uint32_t n, void *p) {
    return transfer(b, l, n, p, false);
}
static int wr(blockdev_t *b, uint32_t l, uint32_t n, const void *p) {
    return transfer(b, l, n, (void *)p, true);
}
void tuh_msc_mount_cb(uint8_t dev) {
    if (tuh_msc_get_block_size(dev, 0) != 512) {
        kprintf("usbmsc: unsupported sector size\n");
        return;
    }
    address = dev;
    bd = (blockdev_t){.name = "usb",
                      .block_size = 512,
                      .block_count = tuh_msc_get_block_count(dev, 0),
                      .read = rd,
                      .write = wr};
    ready = true;
    mount_pending = true;
    kprintf("usbmsc: TinyUSB ready, %u sectors\n", bd.block_count);
}
void tuh_msc_umount_cb(uint8_t dev) {
    if (dev == address) {
        ready = false;
        mount_pending = false;
        detach_pending = true;
    }
}
void usbmsc_service(void) {
    if (detach_pending) {
        detach_pending = false;
        usb_fat_detach();
        kprintf("usbmsc: detached\n");
    }
    if (mount_pending) {
        mount_pending = false;
        usb_fat_mount(&bd);
    }
}
blockdev_t *usbmsc_device(void) { return ready ? &bd : NULL; }
void usbmsc_detach(void) {
    ready = false;
    detach_pending = true;
}
