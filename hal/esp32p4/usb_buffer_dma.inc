/* Original, polled DWC2 buffer-DMA host engine. Included after the cache helpers.
 * Register definitions are ESP-IDF's; transaction sequencing is implemented here.
 * References (no TinyUSB source is incorporated): DWC2 programming model and
 * https://github.com/hathach/tinyusb/blob/master/src/portable/synopsys/dwc2/hcd_dwc2.c
 * Buffer DMA is required for full-/low-speed transactions through a high-speed TT.
 * One hardware packet is armed at a time, so short packets and split retries do
 * not advance the user buffer or DATA PID until a completed transaction.
 */
#define BDM_PIPES 6u
#define BDM_BYTES 1024u
#define BDM_CHENA (1u << 31)
#define BDM_CHDIS (1u << 30)
#define BDM_XFRC (1u << 0)
#define BDM_HALT (1u << 1)
#define BDM_AHB (1u << 2)
#define BDM_STALL (1u << 3)
#define BDM_NAK (1u << 4)
#define BDM_ACK (1u << 5)
#define BDM_NYET (1u << 6)
#define BDM_TXERR (1u << 7)
#define BDM_BABBLE (1u << 8)
#define BDM_FRAME (1u << 9)
#define BDM_TOGGLE (1u << 10)
#define BDM_SPLIT (1u << 31)
#define BDM_COMPLETE (1u << 16)
#define BDM_TIMEOUT_US 500000u

typedef struct {
    usbh_speed_t speed;
    uint8_t hub, port;
    bool valid;
} bdm_route_t;
static bdm_route_t bdm_routes[16];
typedef struct {
    bool open, active, running, closing, quarantined, in, periodic, split;
    bool complete_split, ping, ping_phase;
    uint8_t address, endpoint, type, pid, errors, nyets;
    uint16_t mps, split_frame;
    uint32_t period_us;
    uint64_t next_us, started_us, packet_us, period_due_us;
    void *user;
    uint8_t *dma;
    size_t length, done, packet_len, programmed;
    uint32_t seen;
    int result;
} bdm_pipe_t;
static bdm_pipe_t bdm_pipes[BDM_PIPES];
static bool bdm_ready;

static usb_dwc_host_chan_regs_t *bdm_regs(unsigned id) { return &s_hal.dev->host_chans[id]; }
static bool bdm_hardware_idle(unsigned id) { return !(bdm_regs(id)->hcchar_reg.val & BDM_CHENA); }
static bool bdm_alloc(bdm_pipe_t *p) {
    if (!p->dma) {
        void *raw = kmalloc_internal(BDM_BYTES + CACHE_LINE - 1);
        if (!raw)
            return false;
        p->dma = (uint8_t *)(((uintptr_t)raw + CACHE_LINE - 1) & ~(uintptr_t)(CACHE_LINE - 1));
    }
    return true;
}
static bool bdm_init(void) {
    if (bdm_ready)
        return true;
    /* Port-enable may set DESCDMA again; the outer HAL also clears it there. */
    s_hal.dev->hcfg_reg.val &= ~((1u << 23) | (1u << 26));
    for (unsigned id = 0; id < BDM_PIPES; id++) {
        bdm_regs(id)->hcintmsk_reg.val = 0;
        bdm_regs(id)->hcint_reg.val = 0x3fffu;
    }
    bdm_ready = true;
    return true;
}
static void bdm_route(uint8_t address, usbh_speed_t speed, uint8_t hub, uint8_t port) {
    if (address >= 16)
        return;
    bdm_routes[address] = (bdm_route_t){.speed = speed, .hub = hub, .port = port, .valid = true};
}
static uint8_t bdm_ep_index(uint8_t ep) {
    return (uint8_t)((ep & 15u) | ((ep & 0x80u) ? 16u : 0u));
}
static void bdm_finish(unsigned id, int result) {
    bdm_pipe_t *p = &bdm_pipes[id];
    p->active = p->running = false;
    p->result = result;
    bdm_regs(id)->hcintmsk_reg.val = 0;
}
static bool bdm_open(unsigned id, uint8_t address, uint8_t endpoint, uint16_t mps, unsigned type,
                     uint8_t interval) {
    if (!bdm_ready || id >= BDM_PIPES || address >= 16 || !mps || mps > BDM_BYTES || type == 1 ||
        type > 3)
        return false;
    bdm_pipe_t *p = &bdm_pipes[id];
    if (p->active || !bdm_hardware_idle(id))
        return false;
    if (!bdm_alloc(p))
        return false;
    bdm_route_t route = bdm_routes[address];
    if (!route.valid)
        route.speed = hal_usbh_speed();
    bool split = route.hub && hal_usbh_speed() == USBH_SPEED_HIGH && route.speed != USBH_SPEED_HIGH;
    if (split && (!route.port || route.hub > 127 || route.port > 127 || mps > 64))
        return false;
    uint8_t *dma = p->dma;
    memset(p, 0, sizeof *p);
    p->dma = dma;
    p->open = true;
    p->address = address;
    p->endpoint = endpoint;
    p->mps = mps;
    p->type = (uint8_t)type;
    p->in = (endpoint & 0x80u) != 0;
    p->periodic = type == 3;
    p->split = split;
    p->result = USBH_PENDING;
    if (p->periodic) {
        if (!interval)
            interval = 1;
        if (route.speed == USBH_SPEED_HIGH) {
            if (interval > 16)
                interval = 16;
            p->period_us = 125u << (interval - 1);
        } else
            p->period_us = (uint32_t)interval * 1000u;
    }
    bdm_regs(id)->hcint_reg.val = 0x3fffu;
    return true;
}
static void bdm_packet_prepare(bdm_pipe_t *p) {
    size_t remain = p->length - p->done;
    p->packet_len = remain < p->mps ? remain : p->mps;
    p->programmed = p->in ? p->mps : p->packet_len;
    if (!p->in && p->packet_len)
        memcpy(p->dma, (const uint8_t *)p->user + p->done, p->packet_len);
    /* Discard stale dirty CPU cache lines before the device can write an IN. */
    cache_write_back(p->dma, BDM_BYTES);
    p->complete_split = false;
    p->nyets = 0;
}
static void bdm_arm(unsigned id, uint64_t now) {
    bdm_pipe_t *p = &bdm_pipes[id];
    usb_dwc_host_chan_regs_t *r = bdm_regs(id);
    if (!p->active || p->closing || p->running || now < p->next_us || !bdm_hardware_idle(id))
        return;
    bdm_route_t route = bdm_routes[p->address];
    if (!route.valid)
        route.speed = hal_usbh_speed();
    uint32_t frame = s_hal.dev->hfnum_reg.val & 0xffffu;
    if (p->split && p->periodic && p->complete_split && (frame >> 3) != p->split_frame) {
        /* A periodic TT completion belongs to its start-split's frame. If task
         * service missed that window, restart safely without advancing DATA. */
        bdm_packet_prepare(p);
        p->next_us = p->period_due_us > now ? p->period_due_us : now;
        return;
    }
    /* No new start-split late in the high-speed frame: leave room for TT completion. */
    if (p->split && !p->complete_split && (frame & 7u) >= 5u) {
        p->next_us = now + 125u;
        return;
    }
    usb_dwc_hcchar_reg_t ch = {.val = 0};
    ch.mps = p->mps;
    ch.epnum = p->endpoint & 15u;
    ch.epdir = p->in;
    ch.lspddev = route.speed == USBH_SPEED_LOW;
    ch.eptype = p->type;
    ch.ec = 1;
    ch.devaddr = p->address;
    ch.oddfrm = p->periodic ? ((frame & 1u) ^ 1u) : 0;
    r->hcchar_reg.val = ch.val;
    uint32_t split = p->split ? BDM_SPLIT | ((uint32_t)route.hub << 7) | route.port : 0;
    /* XACTPOS_ALL = 3; only non-isochronous, one-packet splits are supported. */
    if (p->split)
        split |= 3u << 14;
    if (p->complete_split)
        split |= BDM_COMPLETE;
    r->hcsplt_reg.val = split;
    r->hcint_reg.val = 0x3fffu;
    p->seen = 0;
    if (!p->complete_split) {
        usb_dwc_hctsiz_reg_t size = {.val = 0};
        size.pktcnt = 1;
        size.pid = p->pid;
        size.xfersize = p->programmed;
        p->ping_phase = p->ping && !p->split && !p->in && p->pid != 3;
        if (p->ping_phase) {
            size.dopng = 1;
            size.xfersize = 0;
        }
        r->hctsiz_reg.val = size.val;
        r->hcdma_reg.val = (uint32_t)(uintptr_t)p->dma;
    } else {
        /* A complete-split OUT carries only a handshake, never the payload a
         * second time. IN receives the packet into the same private DMA span. */
        usb_dwc_hctsiz_reg_t size = {.val = 0};
        size.pktcnt = 1;
        size.pid = p->pid;
        size.xfersize = p->in ? p->programmed : 0;
        r->hctsiz_reg.val = size.val;
        r->hcdma_reg.val = (uint32_t)(uintptr_t)p->dma;
    }
    /* In buffer DMA, channel-halted is sufficient to receive all terminal causes. */
    r->hcintmsk_reg.val = BDM_HALT;
    s_hal.dev->haintmsk_reg.val |= 1u << id;
    s_hal.dev->gintmsk_reg.val |= 1u << 25;
    uint32_t irq=arch_irq_disable();
    frame=s_hal.dev->hfnum_reg.val & 0xffffu;
    if(p->split && !p->complete_split && (frame&7u)>=5u) {
        p->next_us=now+125u; arch_irq_restore(irq); return;
    }
    if(p->split && p->periodic && p->complete_split && (frame>>3)!=p->split_frame) {
        arch_irq_restore(irq);
        bdm_packet_prepare(p);
        p->next_us=p->period_due_us>now?p->period_due_us:now;
        return;
    }
    ch.oddfrm=p->periodic ? ((frame&1u)^1u) : 0;
    p->running = true;
    p->packet_us = now;
    if (p->split && !p->complete_split)
        p->split_frame = (uint16_t)(frame >> 3);
    if (p->periodic && !p->complete_split)
        p->period_due_us = now + p->period_us;
    r->hcchar_reg.val = ch.val | BDM_CHENA;
    arch_irq_restore(irq);
}
static bool bdm_start(unsigned id, void *user, size_t length, int pid) {
    if (id >= BDM_PIPES || length > 65536u || (!user && length && id != 2))
        return false;
    bdm_pipe_t *p = &bdm_pipes[id];
    if (!p->open || p->active || p->quarantined || !bdm_hardware_idle(id))
        return false;
    p->user = user;
    p->length = length;
    p->done = 0;
    p->pid = pid < 0 ? (s_ep_pid[p->address][bdm_ep_index(p->endpoint)] ? 2 : 0) : (uint8_t)pid;
    if (p->pid != 0 && p->pid != 2 && p->pid != 3)
        return false;
    p->started_us = hal_time_now_us();
    p->next_us =
        (p->periodic && p->period_due_us > p->started_us) ? p->period_due_us : p->started_us;
    p->errors = 0;
    p->result = USBH_PENDING;
    p->active = true;
    bdm_packet_prepare(p);
    bdm_arm(id, p->started_us);
    return true;
}
static void bdm_retry(unsigned id, uint64_t now, bool new_start, uint32_t delay) {
    bdm_pipe_t *p = &bdm_pipes[id];
    if (new_start)
        bdm_packet_prepare(p);
    p->running = false;
    p->next_us = now + delay;
}
static void bdm_step(unsigned id, uint64_t now) {
    bdm_pipe_t *p = &bdm_pipes[id];
    if (!p->active)
        return;
    usb_dwc_host_chan_regs_t *r = bdm_regs(id);
    if (p->running) {
        uint32_t intr = r->hcint_reg.val;
        p->seen |= intr;
        if (!(intr & BDM_HALT))
            return; /* Do not clear an ACK that precedes the halt. */
        intr = p->seen;
        r->hcint_reg.val = intr;
        p->seen = 0;
        p->running = false;
        if (p->closing) {
            bdm_finish(id, USBH_ERR_XFER);
            return;
        }
        if (intr & BDM_STALL) {
            bdm_finish(id, USBH_ERR_STALL);
            return;
        }
        if (intr & (BDM_AHB | BDM_BABBLE)) {
            bdm_finish(id, USBH_ERR_XFER);
            return;
        }
        /* A high-speed OUT NYET acknowledges this data packet and asks the
         * host to PING before the next. Only count it without XFRC when the
         * hardware packet counter confirms acceptance; an ambiguous halt must
         * fail rather than replay a possibly accepted payload. */
        if (!p->split && !p->in && !p->ping_phase && (intr & BDM_NYET) && !(intr & BDM_XFRC)) {
            if (r->hctsiz_reg.pktcnt == 0)
                intr |= BDM_XFRC;
            else {
                bdm_finish(id, USBH_ERR_XFER);
                return;
            }
        }
        if (p->ping_phase) {
            if (intr & BDM_ACK) {
                p->ping = p->ping_phase = false;
                bdm_retry(id, now, true, 0);
            } else if (intr & (BDM_NAK | BDM_NYET))
                bdm_retry(id, now, true, 125);
            else if (++p->errors >= 3)
                bdm_finish(id, USBH_ERR_XFER);
            else
                bdm_retry(id, now, true, 1000);
        } else if (intr & BDM_XFRC) {
            /* An OUT start-split ACK is not the endpoint's completed transfer. */
            if (p->split && !p->complete_split) {
                p->complete_split = true;
                p->next_us = now;
            } else {
                size_t actual = p->packet_len;
                if (p->in) {
                    size_t remain = r->hctsiz_reg.xfersize;
                    if (remain > p->programmed) {
                        bdm_finish(id, USBH_ERR_XFER);
                        return;
                    }
                    actual = p->programmed - remain;
                    if (actual > p->length - p->done) {
                        bdm_finish(id, USBH_ERR_SIZE);
                        return;
                    }
                    cache_invalidate(p->dma, BDM_BYTES);
                    if (actual && p->user)
                        memcpy((uint8_t *)p->user + p->done, p->dma, actual);
                }
                p->done += actual;
                if (p->pid != 3)
                    p->pid ^= 2u;
                if (p->endpoint & 15u)
                    s_ep_pid[p->address][bdm_ep_index(p->endpoint)] = p->pid == 2;
                p->errors = 0;
                if (!p->split && !p->in && (intr & BDM_NYET))
                    p->ping = true;
                if (p->done >= p->length || (p->in && actual < p->mps)) {
                    bdm_finish(id, (int)p->done);
                    return;
                }
                bdm_retry(id, now, true, p->periodic ? p->period_us : 0);
            }
        } else if (p->split && (intr & BDM_NYET)) {
            if (!p->complete_split) {
                bdm_retry(id, now, true, 1000);
            } else if (++p->nyets >= (p->periodic ? 3u : 6u)) {
                /* The TT did not complete this window. A fresh start-split is
                 * needed in a later frame; NYET is not a keyboard disconnect. */
                bdm_retry(id, now, true, p->periodic ? p->period_us : 1000);
            } else
                bdm_retry(id, now, false, 0);
        } else if (p->split && !p->complete_split && (intr & BDM_ACK)) {
            p->complete_split = true;
            p->next_us = now;
        } else if (intr & (BDM_NAK | BDM_FRAME | BDM_TOGGLE)) {
            p->errors = 0;
            bdm_retry(id, now, true, p->periodic ? p->period_us : 125);
        } else if (intr & BDM_TXERR) {
            if (++p->errors >= 3) {
                bdm_finish(id, USBH_ERR_XFER);
                return;
            }
            bdm_retry(id, now, true, p->split ? 1000 : 125);
        } else {
            if (++p->errors >= 3) {
                bdm_finish(id, USBH_ERR_XFER);
                return;
            }
            bdm_retry(id, now, true, 1000);
        }
    }
    bdm_arm(id, now);
}
static void bdm_service(void) {
    if (!bdm_ready)
        return;
    uint64_t begin = hal_time_now_us();
    for (;;) {
        uint64_t now = hal_time_now_us();
        bool split_window = false;
        for (unsigned id = 0; id < BDM_PIPES; id++) {
            bdm_step(id, now);
            bdm_pipe_t *p = &bdm_pipes[id];
            if (p->active && p->split &&
                (p->running || p->complete_split || p->next_us <= now + 125u))
                split_window = true;
        }
        /* Complete split handshakes before yielding to a task that polls at ms
         * intervals. Idle NAK endpoints defer until their scheduled interval. */
        if (!split_window || now - begin >= 1200u)
            break;
    }
}
static int bdm_poll(unsigned id) {
    if (id >= BDM_PIPES || !bdm_pipes[id].open)
        return USBH_ERR_XFER;
    bdm_service();
    bdm_pipe_t *p = &bdm_pipes[id];
    return p->active ? USBH_PENDING : p->result;
}
static void bdm_close(unsigned id) {
    if (id >= BDM_PIPES)
        return;
    bdm_pipe_t *p = &bdm_pipes[id];
    if (!p->open && !p->quarantined)
        return;
    p->closing = true;
    usb_dwc_host_chan_regs_t *r = bdm_regs(id);
    uint64_t begin = hal_time_now_us();
    /* Split and periodic channels normally halt after the current transaction.
     * Do not disable one in the middle of a TT's split sequence. */
    while (!bdm_hardware_idle(id) && hal_time_now_us() - begin < 20000u) {
        uint32_t intr = r->hcint_reg.val;
        if ((intr & BDM_HALT) != 0)
            break;
        if ((!p->split && !p->periodic) || (intr & (BDM_NAK | BDM_FRAME)))
            r->hcchar_reg.val |= BDM_CHDIS | BDM_CHENA;
    }
    bool idle = bdm_hardware_idle(id) || (r->hcint_reg.val & BDM_HALT);
    p->open = p->active = p->running = false;
    p->quarantined = !idle;
    p->result = idle ? USBH_ERR_XFER : USBH_ERR_TIMEOUT;
    r->hcintmsk_reg.val = 0;
    s_hal.dev->haintmsk_reg.val &= ~(1u << id);
    if (idle) {
        r->hcint_reg.val = 0x3fffu;
    }
    /* A timed-out halt is quarantined: the DMA buffer remains owned by hardware
     * and cannot be freed or re-used until a later reset leaves CHENA clear. */
}
static int bdm_keyboard_poll(uint8_t report[8]) {
    int n = bdm_poll(2);
    if (n >= 0) {
        if (n < 8)
            return n;
        memcpy(report, bdm_pipes[2].dma, 8);
        return 8;
    }
    return n;
}
static int bdm_wait_stage(void *buffer, size_t length, bool in, int pid) {
    bdm_pipes[0].in = in;
    if (!bdm_start(0, buffer, length, pid))
        return USBH_ERR_XFER;
    uint64_t begin = hal_time_now_us();
    while (hal_time_now_us() - begin < BDM_TIMEOUT_US) {
        int n = bdm_poll(0);
        if (n != USBH_PENDING)
            return n;
        if (!hal_usbh_connected()) {
            bdm_close(0);
            return USBH_ERR_XFER;
        }
    }
    bdm_close(0);
    return USBH_ERR_TIMEOUT;
}
static int bdm_control(const uint8_t setup[8], void *data, size_t length) {
    if (!bdm_pipes[0].open || !setup)
        return USBH_ERR_XFER;
    size_t wanted = (size_t)setup[6] | ((size_t)setup[7] << 8);
    if (wanted > length || (wanted && !data))
        return USBH_ERR_SIZE;
    int n = bdm_wait_stage((void *)setup, 8, false, 3);
    if (n != 8)
        return n < 0 ? n : USBH_ERR_XFER;
    bool in = (setup[0] & 0x80u) != 0;
    int moved = 0;
    if (wanted) {
        moved = bdm_wait_stage(data, wanted, in, 2);
        if (moved < 0)
            return moved;
    }
    uint8_t status[CACHE_LINE] __attribute__((aligned(CACHE_LINE)));
    n = bdm_wait_stage(status, 0, wanted ? !in : true, 2);
    if (n < 0)
        return n;
    /* Configuration and successful CLEAR_FEATURE(ENDPOINT_HALT) reset toggles. */
    uint8_t address = bdm_pipes[0].address;
    if (setup[0] == 0 && setup[1] == 9)
        memset(s_ep_pid[address], 0, sizeof s_ep_pid[address]);
    if (setup[0] == 2 && setup[1] == 1 && setup[2] == 0 && setup[3] == 0)
        s_ep_pid[address][bdm_ep_index(setup[4])] = 0;
    return moved;
}
