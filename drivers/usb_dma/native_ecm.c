/* CDC-ECM enumeration, with read-only RTL8153 PHY status for bring-up.
 * Implemented from USB descriptors and class requests; no Linux driver code. */
#include "native_ecm.h"
#include "hal_usbh_dma.h"
#include "native_msc.h"
#include <m5os/ethernet.h>
#include <m5os/hal_time.h>
#include <m5os/kmalloc.h>
#include <m5os/kprintf.h>
#include <m5os/net_external.h>
#include <string.h>

static struct {
    uint8_t address, control, data, alt, in, out, notify, mac_index, config;
    uint16_t ep0, imps, omps, mtu, nmps;
    uint8_t interval;
    uint8_t mac[6];
    bool candidate, ready, link;
    uint64_t status_at;
} eth;
static int request(uint8_t type, uint8_t req, uint16_t value, uint16_t index, void *data,
                   uint16_t len) {
    if (!hal_usbh_open_control(eth.address, eth.ep0))
        return -1;
    uint8_t setup[8] = {type, req, value, value >> 8, index, index >> 8, len, len >> 8};
    return hal_usbh_control(setup, data, len);
}
static int hex(uint8_t c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
static bool read_mac(void) {
    uint8_t d[64];
    uint16_t lang = 0x0409;
    if (request(0x80, 6, 0x0300, 0, d, 4) >= 4)
        lang = d[2] | d[3] << 8;
    int n = request(0x80, 6, 0x0300 | eth.mac_index, lang, d, sizeof d);
    if (n < 26 || d[1] != 3 || d[0] < 26)
        return false;
    unsigned any = 0;
    for (unsigned i = 0; i < 6; i++) {
        int a = hex(d[2 + i * 4]), b = hex(d[4 + i * 4]);
        if (a < 0 || b < 0 || d[3 + i * 4] || d[5 + i * 4])
            return false;
        eth.mac[i] = (a << 4) | b;
        any |= eth.mac[i];
    }
    return any && !(eth.mac[0] & 1);
}
bool native_ecm_probe(uint8_t address, uint16_t ep0_mps, uint8_t configurations, int *config) {
    if (eth.ready)
        return false; /* One Ethernet adapter for now. */
    memset(&eth, 0, sizeof eth);
    eth.address = address;
    eth.ep0 = ep0_mps;
    for (unsigned ci = 0; ci < configurations && ci < 4; ci++) {
        uint8_t cfg[1024];
        int n = request(0x80, 6, 0x0200 | ci, 0, cfg, 9);
        if (n != 9)
            continue;
        unsigned total = cfg[2] | cfg[3] << 8;
        if (total < 9 || total > sizeof cfg)
            continue;
        n = request(0x80, 6, 0x0200 | ci, 0, cfg, total);
        if (n != (int)total)
            continue;
        bool comm = false, have_comm = false, have_data = false;
        uint8_t current = 0, alt = 0, cls = 0, sub = 0;
        eth.in = eth.out = eth.notify = eth.mac_index = 0;
        for (unsigned i = 0; i + 2 <= total;) {
            unsigned len = cfg[i];
            if (len < 2 || i + len > total)
                break;
            uint8_t type = cfg[i + 1];
            if (type == 4 && len >= 9) {
                current = cfg[i + 2];
                alt = cfg[i + 3];
                cls = cfg[i + 5];
                sub = cfg[i + 6];
                comm = cls == 2 && sub == 6;
                if (comm) {
                    have_comm = true;
                    eth.control = current;
                }
                if (cls == 10 && alt) {
                    eth.data = current;
                    eth.alt = alt;
                }
            } else if (type == 0x24 && len >= 13 && comm && cfg[i + 2] == 0x0f) {
                eth.mac_index = cfg[i + 3];
                eth.mtu = cfg[i + 8] | cfg[i + 9] << 8;
            } else if (type == 5 && len >= 7) {
                uint8_t ep = cfg[i + 2], kind = cfg[i + 3] & 3;
                uint16_t mps = (cfg[i + 4] | cfg[i + 5] << 8) & 0x7ff;
                if (comm && kind == 3 && (ep & 0x80)) {
                    eth.notify = ep;
                    eth.nmps = mps;
                    eth.interval = cfg[i + 6];
                }
                if (cls == 10 && alt && kind == 2 && mps && mps <= 1024) {
                    if (ep & 0x80) {
                        eth.in = ep;
                        eth.imps = mps;
                    } else {
                        eth.out = ep;
                        eth.omps = mps;
                    }
                    have_data = eth.in && eth.out;
                }
            }
            i += len;
        }
        kprintf("usbeth: config=%u CDC-ECM=%u data=%u in=%02x out=%02x\n", cfg[5], have_comm,
                have_data, eth.in, eth.out);
        if (have_comm && have_data && eth.mac_index && eth.mtu >= 1514 && read_mac()) {
            eth.config = cfg[5];
            eth.candidate = true;
            *config = eth.config;
            return true;
        }
    }
    return false;
}

#define FRAME_MAX 1536
#define QUEUE_DEPTH 8
struct frame {
    uint16_t length;
    uint8_t bytes[FRAME_MAX];
};
static struct frame *rxq, *txq;
static unsigned rxhead, rxtail, rxcount, txhead, txtail, txcount;
static mutex_t queue_lock = MUTEX_INIT;
static uint8_t *rxbuf, *txbuf, *notification;
static bool rxpending, txpending, npending, txzlp;
static unsigned txlength;
static uint64_t tx_started, last_service;
static ethernet_status_t counters;
/* Queue callbacks run while the network mutex is held. They never acquire
 * the USB mutex or call back into the network stack. */
static bool enqueue_tx(const uint8_t *frame, size_t len) {
    if (!frame || len < 14 || len > 1514)
        return false;
    mutex_lock(&queue_lock);
    bool ok = eth.ready && eth.link && txq && txcount < QUEUE_DEPTH;
    if (ok) {
        struct frame *f = &txq[txtail];
        f->length = len < 60 ? 60 : len;
        memcpy(f->bytes, frame, len);
        if (len < 60)
            memset(f->bytes + len, 0, 60 - len);
        txtail = (txtail + 1) % QUEUE_DEPTH;
        txcount++;
    } else
        counters.dropped++;
    mutex_unlock(&queue_lock);
    return ok;
}
static int dequeue_rx(uint8_t *frame, size_t cap) {
    mutex_lock(&queue_lock);
    int n = 0;
    if (rxq && rxcount) {
        struct frame *f = &rxq[rxhead];
        if (f->length <= cap) {
            n = f->length;
            memcpy(frame, f->bytes, n);
        } else
            counters.dropped++;
        rxhead = (rxhead + 1) % QUEUE_DEPTH;
        rxcount--;
    }
    mutex_unlock(&queue_lock);
    return n;
}
static void clear_queues(void) {
    mutex_lock(&queue_lock);
    rxhead = rxtail = rxcount = txhead = txtail = txcount = 0;
    mutex_unlock(&queue_lock);
}
bool native_ecm_attach(void) {
    if (!eth.candidate)
        return false;
    if (!rxq)
        rxq = kmalloc_psram(sizeof *rxq * QUEUE_DEPTH);
    if (!txq)
        txq = kmalloc_psram(sizeof *txq * QUEUE_DEPTH);
    if (!rxbuf)
        rxbuf = kmalloc_psram(2048 + FRAME_MAX + 64);
    if (!rxq || !txq || !rxbuf)
        return false;
    txbuf = rxbuf + 2048;
    notification = txbuf + FRAME_MAX;
    clear_queues();
    memset(&counters, 0, sizeof counters);
    rxpending = txpending = npending = txzlp = false;
    mutex_lock(&usb_native_bus_lock);
    bool ok = request(0x01, 0x0b, eth.alt, eth.data, NULL, 0) >= 0 &&
              request(0x21, 0x43, 0x000c, eth.control, NULL, 0) >= 0;
    if (ok)
        hal_usbh_prepare_device(eth.address, false);
    if (ok)
        ok = hal_usbh_net_open(0, eth.address, eth.in, eth.imps, false, 0) &&
             hal_usbh_net_open(1, eth.address, eth.out, eth.omps, false, 0);
    if (ok && eth.notify && eth.nmps <= 64)
        ok = hal_usbh_net_open(2, eth.address, eth.notify, eth.nmps, true, eth.interval);
    if (!ok)
        for (unsigned i = 0; i < 3; i++)
            hal_usbh_net_close(i);
    eth.ready = ok;
    mutex_unlock(&usb_native_bus_lock);
    if (ok)
        kprintf("usbeth: ECM ready address=%u control=%u data=%u alt=%u mtu=%u "
                "MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
                eth.address, eth.control, eth.data, eth.alt, eth.mtu, eth.mac[0], eth.mac[1],
                eth.mac[2], eth.mac[3], eth.mac[4], eth.mac[5]);
    return ok;
}
void native_ecm_service(void) {
    if (!eth.ready)
        return;
    bool link = eth.link;
    uint64_t now = hal_time_now_us();
    if (last_service && now - last_service > counters.max_service_gap_us)
        counters.max_service_gap_us = now - last_service;
    last_service = now;
    mutex_lock(&usb_native_bus_lock);
    uint64_t waited = hal_time_now_us() - now;
    if (waited > counters.max_bus_wait_us)
        counters.max_bus_wait_us = waited;
    if (hal_time_now_us() - eth.status_at >= 500000u) {
        eth.status_at = hal_time_now_us();
        uint8_t status[4];
        if (request(0xc0, 5, 0xe908, 0x0100, status, sizeof status) == 4)
            link = (status[0] & 2) != 0;
    }
    if (eth.notify && eth.nmps <= 64) {
        if (npending) {
            int n = hal_usbh_net_poll(2);
            if (n != USBH_PENDING) {
                npending = false;
                if (n >= 8 && notification[0] == 0xa1 && notification[4] == eth.control &&
                    notification[5] == 0) {
                    if (notification[1] == 0 && notification[6] == 0 && notification[7] == 0)
                        link = notification[2] != 0;
                    if (notification[1] == 0x2a && n >= 16 && notification[6] == 8) {
                        uint32_t bps = (uint32_t)notification[8] | (uint32_t)notification[9] << 8 |
                                       (uint32_t)notification[10] << 16 |
                                       (uint32_t)notification[11] << 24;
                        counters.speed_mbps = bps / 1000000u;
                    }
                } else if (n < 0)
                    counters.usb_errors++;
            }
        }
        if (!npending)
            npending = hal_usbh_net_start(2, notification, eth.nmps);
    }
    if (rxpending) {
        int n = hal_usbh_net_poll(0);
        if (n != USBH_PENDING) {
            rxpending = false;
            if (n >= 14 && n <= 1514) {
                counters.rx_frames++;
                mutex_lock(&queue_lock);
                if (rxcount < QUEUE_DEPTH) {
                    rxq[rxtail].length = n;
                    memcpy(rxq[rxtail].bytes, rxbuf, n);
                    rxtail = (rxtail + 1) % QUEUE_DEPTH;
                    rxcount++;
                } else
                    counters.dropped++;
                mutex_unlock(&queue_lock);
            } else if (n < 0)
                counters.usb_errors++;
            else if (n)
                counters.dropped++;
        }
    }
    if (!rxpending)
        rxpending = hal_usbh_net_start(0, rxbuf, 2048);
    if (txpending) {
        int n = hal_usbh_net_poll(1);
        if (n == USBH_PENDING && hal_time_now_us() - tx_started > 3000000u) {
            kprintf("usbeth: TX timeout bytes=%u zlp=%u\n", txlength, txzlp);
            hal_usbh_dump();
            hal_usbh_net_close(1);
            hal_usbh_net_open(1, eth.address, eth.out, eth.omps, false, 0);
            n = USBH_ERR_TIMEOUT;
        }
        if (n != USBH_PENDING) {
            txpending = false;
            if (n < 0) {
                counters.usb_errors++;
                txlength = 0;
                txzlp = false;
            } else if (!txzlp && txlength && txlength % eth.omps == 0) {
                txzlp = true;
                txpending = hal_usbh_net_start(1, txbuf, 0);
                tx_started = hal_time_now_us();
                if (!txpending) {
                    counters.usb_errors++;
                    txlength = 0;
                    txzlp = false;
                }
            } else {
                counters.tx_frames++;
                txlength = 0;
                txzlp = false;
            }
        }
    }
    if (!txpending) {
        mutex_lock(&queue_lock);
        if (txcount) {
            txlength = txq[txhead].length;
            memcpy(txbuf, txq[txhead].bytes, txlength);
            txhead = (txhead + 1) % QUEUE_DEPTH;
            txcount--;
        }
        mutex_unlock(&queue_lock);
        if (txlength) {
            txpending = hal_usbh_net_start(1, txbuf, txlength);
            tx_started = hal_time_now_us();
            if (!txpending) {
                counters.usb_errors++;
                txlength = 0;
            }
        }
    }
    mutex_unlock(&usb_native_bus_lock);
    if (link != eth.link) {
        eth.link = link;
        kprintf("usbeth: link %s\n", link ? "up" : "down");
        if (link) {
            clear_queues();
            net_external_up(eth.mac, enqueue_tx, dequeue_rx);
        } else {
            net_external_down();
            clear_queues();
        }
    }
    now = hal_time_now_us();
    net_external_poll();
    uint64_t elapsed = hal_time_now_us() - now;
    if (elapsed > counters.max_network_poll_us)
        counters.max_network_poll_us = elapsed;
}
void native_ecm_detach(void) {
    bool was_link = eth.link;
    eth.ready = false;
    eth.link = false;
    if (was_link)
        net_external_down();
    mutex_lock(&usb_native_bus_lock);
    for (unsigned i = 0; i < 3; i++)
        hal_usbh_net_close(i);
    mutex_unlock(&usb_native_bus_lock);
    clear_queues();
    rxpending = txpending = npending = false;
    txlength = 0;
    txzlp = false;
    memset(&eth, 0, sizeof eth);
}
void usb_ethernet_status(ethernet_status_t *out) {
    if (!out)
        return;
    *out = counters;
    out->detected = eth.candidate;
    out->ready = eth.ready;
    out->link = eth.link;
    memcpy(out->mac, eth.mac, 6);
    wifi_status_t net;
    out->active = net_external_status(&net);
    if (out->active) {
        memcpy(out->ip, net.ip, 4);
        memcpy(out->netmask, net.netmask, 4);
        memcpy(out->gateway, net.gateway, 4);
    }
}
