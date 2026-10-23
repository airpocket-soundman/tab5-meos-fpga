/* MSC BOT/SCSI, three independent LUN-0 devices. No automatic formatting. */
#include "native_msc.h"
#include "hal_usbh_dma.h"
#include <m5os/errno.h>
#include <m5os/hal_time.h>
#include <m5os/kprintf.h>
#include <m5os/task.h>
#include <string.h>

typedef struct {
    uint8_t address, in, out;
    uint16_t imps, omps;
    bool ready;
    uint32_t tag;
    blockdev_t bd;
} msc_t;
static msc_t devices[NATIVE_MSC_SLOTS];
mutex_t usb_native_bus_lock = MUTEX_INIT;
static mutex_t msc_lock = MUTEX_INIT;
static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static void putbe(uint8_t *p, uint32_t n) {
    p[0] = n >> 24;
    p[1] = n >> 16;
    p[2] = n >> 8;
    p[3] = n;
}
static void putle(uint8_t *p, uint32_t n) {
    p[0] = n;
    p[1] = n >> 8;
    p[2] = n >> 16;
    p[3] = n >> 24;
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
/* msc_lock protects a complete BOT transaction. The HAL lock protects only
 * register/channel operations, never the time spent waiting for DMA. */
static int bulk(msc_t *d, bool in, void *buf, size_t n) {
    mutex_lock(&usb_native_bus_lock);
    bool opened =
        hal_usbh_data_open(d->address, in ? d->in : d->out, in ? d->imps : d->omps, false, 0);
    bool started = opened && hal_usbh_data_start(buf, n) >= 0;
    if (!started)
        hal_usbh_data_close();
    mutex_unlock(&usb_native_bus_lock);
    if (!started)
        return ERR_IO;
    uint64_t start = hal_time_now_us();
    int r;
    for (;;) {
        mutex_lock(&usb_native_bus_lock);
        r = hal_usbh_data_poll();
        bool timed_out =
            r == USBH_PENDING && (!hal_usbh_connected() || hal_time_now_us() - start > 3000000u);
        if (r != USBH_PENDING || timed_out)
            hal_usbh_data_close();
        mutex_unlock(&usb_native_bus_lock);
        if (timed_out) {
            kprintf("native-dma: bulk timeout address=%u ep=%02x bytes=%u\n", d->address,
                    in ? d->in : d->out, (unsigned)n);
            return ERR_IO;
        }
        if (r != USBH_PENDING)
            break;
        task_sleep(1);
    }
    if (r != (int)n)
        kprintf("native-dma: address=%u bulk result=%d expected=%u ep=%02x\n", d->address, r,
                (unsigned)n, in ? d->in : d->out);
    return r;
}
static int scsi(msc_t *d, const uint8_t *cdb, int cdbn, void *data, uint32_t n, bool in) {
    uint8_t cbw[31] = {0}, csw[13];
    uint32_t tag = ++d->tag;
    putle(cbw, 0x43425355);
    putle(cbw + 4, tag);
    putle(cbw + 8, n);
    cbw[12] = in ? 0x80 : 0;
    cbw[14] = cdbn;
    memcpy(cbw + 15, cdb, cdbn);
    if (bulk(d, false, cbw, 31) != 31)
        return ERR_IO;
    for (uint32_t off = 0; off < n;) {
        uint32_t part = n - off > 4096 ? 4096 : n - off;
        if (bulk(d, in, (uint8_t *)data + off, part) != (int)part)
            return ERR_IO;
        off += part;
    }
    if (bulk(d, true, csw, 13) != 13 || le32(csw) != 0x53425355 || le32(csw + 4) != tag ||
        le32(csw + 8) > n)
        return ERR_IO;
    return csw[12] == 0 && le32(csw + 8) == 0 ? 0 : ERR_IO;
}
static int rw(blockdev_t *bd, uint32_t lba, uint32_t count, void *buf, bool write) {
    if (!buf || !count || lba >= bd->block_count || count > bd->block_count - lba)
        return ERR_INVAL;
    msc_t *d = bd->priv;
    int rc = 0;
    for (uint32_t i = 0; i < count && !rc;) {
        uint32_t n = count - i > 8 ? 8 : count - i;
        /* A complete BOT command is atomic; a large filesystem request is not.
         * Let Ethernet and HID service their own channels between commands. */
        mutex_lock(&msc_lock);
        if (!d->ready)
            rc = ERR_NODEV;
        else {
            uint8_t cmd[10] = {0};
            cmd[0] = write ? 0x2a : 0x28;
            putbe(cmd + 2, lba + i);
            cmd[7] = n >> 8;
            cmd[8] = n;
            rc = scsi(d, cmd, 10, (uint8_t *)buf + i * 512, n * 512, !write);
            if (rc)
                d->ready = false;
        }
        mutex_unlock(&msc_lock);
        i += n;
        task_yield();
    }
    return rc;
}
static int rd(blockdev_t *b, uint32_t l, uint32_t n, void *p) { return rw(b, l, n, p, false); }
static int wr(blockdev_t *b, uint32_t l, uint32_t n, const void *p) {
    return rw(b, l, n, (void *)p, true);
}
blockdev_t *usbmsc_device_slot(unsigned slot) {
    return slot < NATIVE_MSC_SLOTS && devices[slot].ready ? &devices[slot].bd : NULL;
}
void usbmsc_detach_slot(unsigned slot) {
    if (slot >= NATIVE_MSC_SLOTS)
        return;
    mutex_lock(&msc_lock);
    devices[slot].ready = false;
    mutex_lock(&usb_native_bus_lock);
    hal_usbh_data_close();
    mutex_unlock(&usb_native_bus_lock);
    mutex_unlock(&msc_lock);
}
bool usbmsc_attach_slot(unsigned slot, uint8_t address, uint8_t iface, uint8_t in, uint16_t imps,
                        uint8_t out, uint16_t omps) {
    (void)iface;
    if (slot >= NATIVE_MSC_SLOTS)
        return false;
    mutex_lock(&msc_lock);
    msc_t *d = &devices[slot];
    d->ready = false;
    d->address = address;
    d->in = in;
    d->out = out;
    d->imps = imps;
    d->omps = omps;
    uint8_t cap[8], cmd[10] = {0x25};
    int rc = ERR_IO;
    for (int i = 0; i < 3 && rc; i++) {
        uint8_t tur[6] = {0}, sensecmd[6] = {3, 0, 0, 0, 18, 0}, sense[18];
        if (scsi(d, tur, 6, NULL, 0, true))
            scsi(d, sensecmd, 6, sense, 18, true);
        rc = scsi(d, cmd, 10, cap, 8, true);
        if (rc)
            task_sleep(100);
    }
    if (!rc && be32(cap + 4) == 512 && be32(cap) != 0xffffffffu) {
        d->bd = (blockdev_t){.name = "usb",
                             .block_size = 512,
                             .block_count = be32(cap) + 1,
                             .read = rd,
                             .write = wr,
                             .priv = d};
        d->ready = true;
        kprintf("usbmsc: slot=%u address=%u ready, %u sectors of 512 bytes\n", slot, address,
                d->bd.block_count);
    } else
        kprintf("usbmsc: slot=%u capacity or transport error\n", slot);
    bool ready = d->ready;
    mutex_unlock(&msc_lock);
    return ready;
}
bool usbmsc_attach(uint8_t iface, uint8_t in, uint16_t imps, uint8_t out, uint16_t omps) {
    return usbmsc_attach_slot(0, 1, iface, in, imps, out, omps);
}
void usbmsc_detach(void) { usbmsc_detach_slot(0); }
blockdev_t *usbmsc_device(void) { return usbmsc_device_slot(0); }
