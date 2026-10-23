/*
 * USB host on the ESP32-P4's high-speed OTG controller (Synopsys DWC2), polled, FreeRTOS-free.
 *
 * What is IDF's and what is ours:
 *  - the PHY (UTMI) power-up and clock gating: IDF's usb_new_phy(), which is plain register code;
 *  - register access and the DMA descriptor format: IDF's hal/usb_dwc_hal.h (HAL = no RTOS inside);
 *  - NOT used: IDF's USB host stack (usb_host, hcd_dwc, hub, enum) - tasks, queues, mutexes.
 * The sequencing (power, FIFO sizes, control stages with PID toggling) follows components/usb/hcd_dwc.c
 * of ESP-IDF v5.4.4, minus everything about queues, multiple pipes and hubs.
 *
 * DMA and the cache: the controller reads and writes internal RAM by DMA, the CPU goes through the L1
 * data cache.  So descriptors and buffers are cache-line aligned, written back before the controller
 * runs (C2M) and invalidated before the CPU reads what it wrote (M2C).
 */
#include "../../drivers/usb_dma/hal_usbh_dma.h"
#include <m5os/hal_time.h>
#include <m5os/hal_gpio.h>
#include <m5os/kprintf.h>
#include <string.h>

#include "esp_idf_version.h"
#include "hal/usb_dwc_hal.h"
#include "esp_private/usb_phy.h"
#include "esp_cache.h"

#define USB_PORT            0           /* ESP32-P4: port 0 is the high-speed core with the UTMI PHY */
#define MAX_CHANNELS        16          /* DWC2 has up to 16 host channels */
#define FIFO_DEPTH_HS_LINES 1024        /* hcd_dwc.c: otg_dfifo_depth for a core with a HS PHY */
#define CACHE_LINE          64u
#define QTD_LIST_ALIGN      512         /* USB_DWC_QTD_LIST_MEM_ALIGN */
#define QTDS_PER_CONTROL    3           /* setup, data, status */
#define MAX_DATA            1024         /* largest data stage we support (config descriptors fit) */
#define STAGE_TIMEOUT_US    500000u
#define SETUP_BYTES         8

/* PIDs for the stages (hcd_dwc.c: _buffer_exec / _buffer_exec_cont) */
#define PID_DATA0  0
#define PID_DATA1  1

static usb_dwc_hal_context_t  s_hal;
static usb_dwc_hal_chan_t    *s_chan_hdls[MAX_CHANNELS];
static usb_dwc_hal_chan_t     s_chan;
static bool                   s_chan_open;
static usb_phy_handle_t       s_phy;
static bool                   s_inited;
static uint16_t               s_mps;

static uint8_t s_qtd[QTD_LIST_ALIGN] __attribute__((aligned(QTD_LIST_ALIGN)));         /* only the first 24 bytes are used */
static uint8_t s_setup[CACHE_LINE]   __attribute__((aligned(CACHE_LINE)));
static uint8_t s_data[MAX_DATA]      __attribute__((aligned(CACHE_LINE)));
static uint32_t s_frames[32] __attribute__((aligned(512)));
static usb_dwc_hal_chan_t s_io;
static bool s_io_open, s_io_active, s_io_in;
static uint16_t s_io_mps;
static void *s_io_user;
static size_t s_io_len, s_io_span;
static uint8_t s_io_ep, s_io_address, s_ep_pid[16][32];
static bool s_low_via_hub[16];
static uint8_t s_io_qtd[512] __attribute__((aligned(512)));
static uint8_t s_io_data[4096] __attribute__((aligned(64)));
/* Independent periodic pipe; bulk I/O must not replace an idle keyboard IN. */
static usb_dwc_hal_chan_t s_kbd;
static bool s_kbd_open, s_kbd_active;
static uint8_t s_kbd_address, s_kbd_ep;
static uint16_t s_kbd_mps;
static usb_dwc_hal_chan_event_t s_kbd_event;
static uint8_t s_kbd_qtd[512] __attribute__((aligned(512)));
static uint8_t s_kbd_data[1024] __attribute__((aligned(64)));

static usb_dwc_hal_chan_event_t decode_channel(usb_dwc_hal_chan_t *c)
{
    usb_dwc_hal_chan_event_t ev = usb_dwc_hal_chan_decode_intr(c);
    if (c == &s_kbd && ev != USB_DWC_HAL_CHAN_EVENT_NONE) s_kbd_event = ev;
    return ev;
}

static void cache_write_back(void *p, size_t n) { esp_cache_msync(p, n, ESP_CACHE_MSYNC_FLAG_DIR_C2M); }
static void cache_invalidate(void *p, size_t n) { esp_cache_msync(p, n, ESP_CACHE_MSYNC_FLAG_DIR_M2C); }

bool hal_usbh_init(void)
{
    if (s_inited) return true;
    usb_phy_config_t phy_cfg = {
        .controller = USB_PHY_CTRL_OTG,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 2)
        .target     = USB_PHY_TARGET_UTMI,
#else
        .target     = USB_PHY_TARGET_INT,
#endif
        .otg_mode   = USB_OTG_MODE_HOST,
        .otg_speed  = USB_PHY_SPEED_UNDEFINED,         /* host: the speed is whatever gets plugged in */
    };
    if (usb_new_phy(&phy_cfg, &s_phy) != ESP_OK) { kprintf("usbh: PHY power-up failed\n"); return false; }

    usb_dwc_hal_init(&s_hal, USB_PORT);
    s_hal.channels.hdls = s_chan_hdls;
    /* Host-mode switching is asynchronous. Wait before programming host FIFOs,
     * following TinyUSB's DWC2 initialization order. */
    uint64_t mode_start = hal_time_now_us();
    while (!(s_hal.dev->gintsts_reg.val & 1u)) {
        if (hal_time_now_us() - mode_start > 100000u) {
            kprintf("usbh: host mode timeout\n");
            return false;
        }
    }
    if (s_hal.constant_config.chan_num_total > MAX_CHANNELS) { kprintf("usbh: unexpected channel count\n"); return false; }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 2)
    /* FIFO split = "balanced" in hcd_dwc.c.  Each unit is one 32-bit line. */
    usb_dwc_hal_fifo_config_t fifo;
    fifo.nptx_fifo_lines = FIFO_DEPTH_HS_LINES / 4;
    fifo.ptx_fifo_lines  = FIFO_DEPTH_HS_LINES / 8;
    fifo.rx_fifo_lines   = s_hal.constant_config.fifo_size - fifo.ptx_fifo_lines - fifo.nptx_fifo_lines;
    if (!usb_dwc_hal_fifo_config_is_valid(&s_hal, &fifo)) { kprintf("usbh: FIFO config invalid\n"); return false; }
    usb_dwc_hal_set_fifo_config(&s_hal, &fifo);
    uint32_t expected_nptx = fifo.rx_fifo_lines | (fifo.nptx_fifo_lines << 16);
    uint32_t expected_ptx = (fifo.rx_fifo_lines + fifo.nptx_fifo_lines) |
                            (fifo.ptx_fifo_lines << 16);
    if (s_hal.dev->gnptxfsiz_reg.val != expected_nptx ||
        s_hal.dev->hptxfsiz_reg.val != expected_ptx) {
        kprintf("usbh: FIFO readback mismatch NPTX=%08x/%08x PTX=%08x/%08x\n",
                s_hal.dev->gnptxfsiz_reg.val, expected_nptx,
                s_hal.dev->hptxfsiz_reg.val, expected_ptx);
        return false;
    }
    kprintf("usbh: host mode ready, FIFO RX=%u NPTX=%08x PTX=%08x\n",
            fifo.rx_fifo_lines, expected_nptx, expected_ptx);
#else
    (void)FIFO_DEPTH_HS_LINES;
    usb_dwc_hal_set_fifo_bias(&s_hal, USB_HAL_FIFO_BIAS_DEFAULT);
#endif

    usb_dwc_hal_port_init(&s_hal);                     /* unmask port/channel interrupt status */
    usb_dwc_hal_port_toggle_power(&s_hal, true);       /* VBUS comes from the expander (board_init); this powers the port logic */
    s_inited = true;
    return true;
}

usbh_event_t hal_usbh_poll(void)
{
    if (!s_inited) return USBH_EVENT_NONE;
    switch (usb_dwc_hal_decode_intr(&s_hal)) {
    case USB_DWC_HAL_PORT_EVENT_CONN:     return USBH_EVENT_CONNECT;
    case USB_DWC_HAL_PORT_EVENT_DISCONN:  return USBH_EVENT_DISCONNECT;
    case USB_DWC_HAL_PORT_EVENT_ENABLED:
        usb_dwc_hal_port_enable(&s_hal);
        memset(s_frames, 0, sizeof s_frames);
        cache_write_back(s_frames, sizeof s_frames);
        usb_dwc_hal_port_set_frame_list(&s_hal, s_frames, USB_HAL_FRAME_LIST_LEN_32);
        usb_dwc_hal_port_periodic_enable(&s_hal);
        return USBH_EVENT_ENABLED;
    case USB_DWC_HAL_PORT_EVENT_DISABLED: return USBH_EVENT_DISABLED;
    case USB_DWC_HAL_PORT_EVENT_OVRCUR:   return USBH_EVENT_OVERCURRENT;
    default:                              return USBH_EVENT_NONE;
    }
}

void hal_usbh_debounce_done(void) { usb_dwc_hal_disable_debounce_lock(&s_hal); }
bool hal_usbh_connected(void)     { return s_inited && usb_dwc_hal_port_check_if_connected(&s_hal); }
void hal_usbh_reset(bool on)      { usb_dwc_hal_port_toggle_reset(&s_hal, on); }

usbh_speed_t hal_usbh_speed(void)
{
    switch (usb_dwc_hal_port_get_conn_speed(&s_hal)) {
    case USB_DWC_SPEED_HIGH: return USBH_SPEED_HIGH;
    case USB_DWC_SPEED_FULL: return USBH_SPEED_FULL;
    default:                 return USBH_SPEED_LOW;
    }
}

bool hal_usbh_open_control(uint8_t dev_addr, uint16_t max_packet)
{
    if (!s_inited) return false;
    if (dev_addr >= 16) return false;
    if (dev_addr == 0) memset(s_ep_pid[0], 0, sizeof s_ep_pid[0]);
    if (s_chan_open) {                                  /* reconfigure: free and allocate again (the channel must be idle) */
        usb_dwc_hal_chan_free(&s_hal, &s_chan);
        s_chan_open = false;
    }
    if (!usb_dwc_hal_chan_alloc(&s_hal, &s_chan, NULL)) return false;
    usb_dwc_hal_ep_char_t ep = {
        .type = USB_DWC_XFER_TYPE_CTRL, .bEndpointAddress = 0, .mps = max_packet,
        .dev_addr = dev_addr, .ls_via_fs_hub = s_low_via_hub[dev_addr],
    };
    usb_dwc_hal_chan_set_ep_char(&s_hal, &s_chan, &ep);
    s_mps = max_packet;
    s_chan_open = true;
    return true;
}

void hal_usbh_prepare_device(uint8_t address, bool low_speed_via_full_speed_hub)
{
    if (address >= 16) return;
    memset(s_ep_pid[address], 0, sizeof s_ep_pid[address]);
    s_low_via_hub[address] = low_speed_via_full_speed_hub;
}

/* Run one stage (one descriptor of the list) to completion.  Returns 0, or a negative USBH_ERR_*. */
static int run_stage(int idx, bool dir_in, int pid)
{
    usb_dwc_hal_chan_set_dir(&s_chan, dir_in);
    usb_dwc_hal_chan_set_pid(&s_chan, pid);
    usb_dwc_hal_chan_activate(&s_chan, s_qtd, QTDS_PER_CONTROL, idx);

    uint64_t start = hal_time_now_us();
    while (hal_time_now_us() - start < STAGE_TIMEOUT_US) {
        if (usb_dwc_hal_decode_intr(&s_hal) != USB_DWC_HAL_PORT_EVENT_CHAN) continue;
        usb_dwc_hal_chan_t *c;
        while ((c = usb_dwc_hal_get_chan_pending_intr(&s_hal)) != NULL) {
            if (c != &s_chan) { decode_channel(c); continue; }
            switch (decode_channel(c)) {
            case USB_DWC_HAL_CHAN_EVENT_CPLT:  return 0;
            case USB_DWC_HAL_CHAN_EVENT_ERROR:
                return usb_dwc_hal_chan_get_error(c) == USB_DWC_HAL_CHAN_ERROR_STALL ? USBH_ERR_STALL : USBH_ERR_XFER;
            default: break;                             /* NONE / HALT_REQ: keep waiting */
            }
        }
    }
    usb_dwc_hal_chan_request_halt(&s_chan);             /* give up: stop the channel so it can be reused */
    start = hal_time_now_us();
    while (hal_time_now_us() - start < STAGE_TIMEOUT_US / 10) {
        if (usb_dwc_hal_decode_intr(&s_hal) != USB_DWC_HAL_PORT_EVENT_CHAN) continue;
        usb_dwc_hal_chan_t *c;
        while ((c = usb_dwc_hal_get_chan_pending_intr(&s_hal)) != NULL) decode_channel(c);
        if (!s_chan.flags.active) break;
    }
    return USBH_ERR_TIMEOUT;
}

int hal_usbh_control(const uint8_t setup[8], void *data, size_t len)
{
    if (!s_chan_open) return USBH_ERR_XFER;
    bool in = setup[0] & 0x80;
    bool has_data = len > 0;
    size_t span = in ? ((len + s_mps - 1) / s_mps) * s_mps : len;      /* IN buffers must be a multiple of the packet size */
    if (span > MAX_DATA) return USBH_ERR_SIZE;

    memset(s_qtd, 0, sizeof s_qtd);
    memcpy(s_setup, setup, SETUP_BYTES);
    cache_write_back(s_setup, CACHE_LINE);
    if (has_data) {
        if (in) memset(s_data, 0, MAX_DATA); else memcpy(s_data, data, len);
        cache_write_back(s_data, MAX_DATA);
    }

    /* Descriptor 0 SETUP, 1 data (skipped if none), 2 status.  HOC = halt the channel after this descriptor, so
     * we get control back between stages and can set the direction and PID the next stage needs. */
    usb_dwc_hal_xfer_desc_fill(s_qtd, 0, s_setup, SETUP_BYTES, USB_DWC_HAL_XFER_DESC_FLAG_SETUP | USB_DWC_HAL_XFER_DESC_FLAG_HOC);
    if (has_data)
        usb_dwc_hal_xfer_desc_fill(s_qtd, 1, s_data, (int)span, (in ? USB_DWC_HAL_XFER_DESC_FLAG_IN : 0) | USB_DWC_HAL_XFER_DESC_FLAG_HOC);
    bool status_in = !(has_data && in);                  /* status stage runs opposite to the data stage; IN if there is none */
    usb_dwc_hal_xfer_desc_fill(s_qtd, 2, NULL, 0, (status_in ? USB_DWC_HAL_XFER_DESC_FLAG_IN : 0) | USB_DWC_HAL_XFER_DESC_FLAG_HOC);
    cache_write_back(s_qtd, CACHE_LINE);

    int r = run_stage(0, false, PID_DATA0);              /* setup stage is always OUT, DATA0 */
    if (r == 0 && has_data) r = run_stage(1, in, PID_DATA1);
    if (r == 0) r = run_stage(2, status_in, PID_DATA1);
    if (r != 0) return r;

    int got = 0;
    if (has_data && in) {
        cache_invalidate(s_qtd, CACHE_LINE);
        cache_invalidate(s_data, MAX_DATA);
        int rem, status;
        usb_dwc_hal_xfer_desc_parse(s_qtd, 1, &rem, &status);
        if (status != USB_DWC_HAL_XFER_DESC_STS_SUCCESS) return USBH_ERR_XFER;
        got = (int)span - rem;
        if ((size_t)got > len) got = (int)len;
        memcpy(data, s_data, (size_t)got);
    } else {
        got = (int)len;
    }
    return got;
}

void hal_usbh_dump(void)
{
    usb_dwc_dev_t *d = s_hal.dev;
    kprintf("usbh: hprt=%08x gintsts=%08x gotgctl=%08x gusbcfg=%08x hcfg=%08x\n",
            (unsigned)d->hprt_reg.val, (unsigned)d->gintsts_reg.val, (unsigned)d->gotgctl_reg.val,
            (unsigned)d->gusbcfg_reg.val, (unsigned)d->hcfg_reg.val);
}

void hal_usbh_data_close(void)
{
    if (!s_io_open) return;
    if (s_io_active) {
        usb_dwc_hal_chan_request_halt(&s_io);
        uint64_t start = hal_time_now_us();
        while (s_io.flags.active && hal_time_now_us() - start < STAGE_TIMEOUT_US) {
            usb_dwc_hal_decode_intr(&s_hal);
            usb_dwc_hal_chan_t *c;
            while ((c = usb_dwc_hal_get_chan_pending_intr(&s_hal)) != NULL)
                decode_channel(c);
        }
    }
    if (s_io.flags.active) return; /* Never reuse DMA storage while hardware owns it. */
    usb_dwc_hal_chan_free(&s_hal, &s_io);
    cache_write_back(s_frames, sizeof s_frames);
    s_io_active = s_io_open = false;
}

bool hal_usbh_data_open(uint8_t address, uint8_t endpoint, uint16_t mps,
                        bool interrupt, uint8_t interval)
{
    hal_usbh_data_close();
    if (s_io_open || address >= 16 || !mps || mps > 1024 || !(endpoint & 15)) return false;
    memset(&s_io, 0, sizeof s_io);
    if (!usb_dwc_hal_chan_alloc(&s_hal, &s_io, NULL)) return false;
    usb_dwc_hal_ep_char_t ep = {0};
    ep.type = interrupt ? USB_DWC_XFER_TYPE_INTR : USB_DWC_XFER_TYPE_BULK;
    ep.bEndpointAddress = endpoint; ep.mps = mps; ep.dev_addr = address;
    ep.ls_via_fs_hub = s_low_via_hub[address];
    ep.periodic.is_hs = hal_usbh_speed() == USBH_SPEED_HIGH;
    ep.periodic.interval = ep.periodic.is_hs ? (1u << ((interval > 0 && interval <= 16 ? interval : 1) - 1)) : (interval ? interval : 1);
    usb_dwc_hal_chan_set_ep_char(&s_hal, &s_io, &ep);
    s_io_ep = (endpoint & 15) + ((endpoint & 0x80) ? 16 : 0);
    s_io_address = address;
    usb_dwc_hal_chan_set_pid(&s_io, s_ep_pid[address][s_io_ep]);
    cache_write_back(s_frames, sizeof s_frames);
    s_io_open = true; s_io_in = (endpoint & 0x80) != 0; s_io_mps = mps;
    return true;
}

int hal_usbh_data_start(void *buf, size_t len)
{
    if (!s_io_open || s_io_active || !buf || !len) return USBH_ERR_XFER;
    size_t span = s_io_in ? ((len + s_io_mps - 1) / s_io_mps) * s_io_mps : len;
    if (span > sizeof s_io_data) return USBH_ERR_SIZE;
    s_io_user = buf; s_io_len = len; s_io_span = span;
    memset(s_io_qtd, 0, sizeof s_io_qtd);
    if (!s_io_in) memcpy(s_io_data, buf, len);
    else memset(s_io_data, 0, span);
    cache_write_back(s_io_data, sizeof s_io_data);
    usb_dwc_hal_xfer_desc_fill(s_io_qtd, 0, s_io_data, span,
        (s_io_in ? USB_DWC_HAL_XFER_DESC_FLAG_IN : 0) | USB_DWC_HAL_XFER_DESC_FLAG_HOC);
    cache_write_back(s_io_qtd, sizeof s_io_qtd);
    usb_dwc_hal_chan_activate(&s_io, s_io_qtd, 1, 0);
    s_io_active = true;
    return 0;
}

int hal_usbh_data_poll(void)
{
    if (!s_io_active) return USBH_ERR_XFER;
    usb_dwc_hal_decode_intr(&s_hal);
    usb_dwc_hal_chan_t *c;
    while ((c = usb_dwc_hal_get_chan_pending_intr(&s_hal)) != NULL) {
        usb_dwc_hal_chan_event_t ev = decode_channel(c);
        if (c != &s_io) continue;
        if (ev == USB_DWC_HAL_CHAN_EVENT_ERROR) {
            s_io_active = false;
            return usb_dwc_hal_chan_get_error(c) == USB_DWC_HAL_CHAN_ERROR_STALL ? USBH_ERR_STALL : USBH_ERR_XFER;
        }
        if (ev == USB_DWC_HAL_CHAN_EVENT_CPLT) {
            s_io_active = false;
            s_ep_pid[s_io_address][s_io_ep] = usb_dwc_hal_chan_get_pid(&s_io);
            cache_invalidate(s_io_qtd, sizeof s_io_qtd);
            int rem, status;
            usb_dwc_hal_xfer_desc_parse(s_io_qtd, 0, &rem, &status);
            if (status != USB_DWC_HAL_XFER_DESC_STS_SUCCESS || rem < 0 || (size_t)rem > s_io_span) return USBH_ERR_XFER;
            size_t got = s_io_span - rem;
            if (got > s_io_len) got = s_io_len;
            if (s_io_in) { cache_invalidate(s_io_data, sizeof s_io_data); memcpy(s_io_user, s_io_data, got); }
            return got;
        }
    }
    return USBH_PENDING;
}

void hal_usbh_keyboard_close(void)
{
    if (!s_kbd_open) return;
    if (s_kbd.flags.active) {
        usb_dwc_hal_chan_request_halt(&s_kbd);
        uint64_t start=hal_time_now_us();
        while (s_kbd.flags.active && hal_time_now_us()-start<STAGE_TIMEOUT_US) {
            usb_dwc_hal_decode_intr(&s_hal);
            usb_dwc_hal_chan_t *c;
            while ((c=usb_dwc_hal_get_chan_pending_intr(&s_hal))!=NULL) decode_channel(c);
        }
    }
    if (s_kbd.flags.active) return; /* Never reuse storage still owned by DMA. */
    usb_dwc_hal_chan_free(&s_hal,&s_kbd);
    cache_write_back(s_frames,sizeof s_frames);
    s_kbd_active=s_kbd_open=false;
    s_kbd_event=USB_DWC_HAL_CHAN_EVENT_NONE;
}
bool hal_usbh_keyboard_open(uint8_t address,uint8_t endpoint,uint16_t mps,uint8_t interval)
{
    hal_usbh_keyboard_close();
    if (s_kbd_open || address>=16 || !(endpoint&0x80) || !(endpoint&15) ||
        mps<8 || mps>sizeof s_kbd_data) return false;
    memset(&s_kbd,0,sizeof s_kbd);
    if (!usb_dwc_hal_chan_alloc(&s_hal,&s_kbd,NULL)) return false;
    usb_dwc_hal_ep_char_t ep={0};
    ep.type=USB_DWC_XFER_TYPE_INTR;
    ep.bEndpointAddress=endpoint;ep.mps=mps;ep.dev_addr=address;
    ep.ls_via_fs_hub=s_low_via_hub[address];
    ep.periodic.is_hs=hal_usbh_speed()==USBH_SPEED_HIGH;
    ep.periodic.interval=ep.periodic.is_hs ?
        (1u<<((interval>0 && interval<=16?interval:1)-1)) : (interval?interval:1);
    usb_dwc_hal_chan_set_ep_char(&s_hal,&s_kbd,&ep);
    cache_write_back(s_frames,sizeof s_frames);
    s_kbd_address=address;s_kbd_ep=(endpoint&15)+16;s_kbd_mps=mps;
    s_kbd_open=true;
    return true;
}
bool hal_usbh_keyboard_start(void)
{
    if (!s_kbd_open || s_kbd_active) return false;
    memset(s_kbd_qtd,0,sizeof s_kbd_qtd);
    memset(s_kbd_data,0,sizeof s_kbd_data);
    cache_write_back(s_kbd_data,sizeof s_kbd_data);
    usb_dwc_hal_chan_set_pid(&s_kbd,s_ep_pid[s_kbd_address][s_kbd_ep]);
    usb_dwc_hal_xfer_desc_fill(s_kbd_qtd,0,s_kbd_data,s_kbd_mps,
        USB_DWC_HAL_XFER_DESC_FLAG_IN|USB_DWC_HAL_XFER_DESC_FLAG_HOC);
    cache_write_back(s_kbd_qtd,sizeof s_kbd_qtd);
    s_kbd_event=USB_DWC_HAL_CHAN_EVENT_NONE;
    usb_dwc_hal_chan_activate(&s_kbd,s_kbd_qtd,1,0);
    s_kbd_active=true;
    return true;
}
int hal_usbh_keyboard_poll(uint8_t report[8])
{
    if (!s_kbd_active) return USBH_ERR_XFER;
    usb_dwc_hal_decode_intr(&s_hal);
    usb_dwc_hal_chan_t *c;
    while ((c=usb_dwc_hal_get_chan_pending_intr(&s_hal))!=NULL) decode_channel(c);
    usb_dwc_hal_chan_event_t ev=s_kbd_event;
    if (ev==USB_DWC_HAL_CHAN_EVENT_ERROR) {
        s_kbd_active=false;
        return USBH_ERR_XFER;
    }
    if (ev!=USB_DWC_HAL_CHAN_EVENT_CPLT) return USBH_PENDING;
    s_kbd_active=false;
    s_ep_pid[s_kbd_address][s_kbd_ep]=usb_dwc_hal_chan_get_pid(&s_kbd);
    cache_invalidate(s_kbd_qtd,sizeof s_kbd_qtd);
    cache_invalidate(s_kbd_data,sizeof s_kbd_data);
    int rem,status;
    usb_dwc_hal_xfer_desc_parse(s_kbd_qtd,0,&rem,&status);
    if (status!=USB_DWC_HAL_XFER_DESC_STS_SUCCESS || rem<0 || rem>s_kbd_mps)
        return USBH_ERR_XFER;
    int got=s_kbd_mps-rem;
    if (got>=8) memcpy(report,s_kbd_data,8);
    return got<8?got:8;
}
