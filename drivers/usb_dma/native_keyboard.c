/*
 * USB keyboard for the Tab5 (USB-A host port).  What is typed goes to the console input (kconsole_inject),
 * so the shell reads it like characters arriving over the serial port.  Started by drivers/tab5kbd when
 * there is no I2C keyboard.
 *
 * Root-device enumeration is shared with USB Mass Storage BOT/SCSI.
 * Boot keyboard reports use the interrupt IN endpoint and the controller periodic
 * schedule. An idle endpoint may NAK indefinitely; this is not a disconnect.
 * Supports a direct device or one external hub with MSC and one boot keyboard.
 * Key codes are HID usage IDs, US layout.
 */
#include "../keyboard/keyboard.h"
#include "../usbkbd/usbkbd.h"
#include "native_msc.h"
#include "native_ecm.h"
#include "../../fs/fat/usb_fat.h"
#include "../usbkbd/hid_keys.h"
#include <m5os/ime.h>
#include "hal_usbh_dma.h"
#include <m5os/kprintf.h>
#include <m5os/task.h>
#include <m5os/hal_time.h>
#include <m5os/usb_info.h>
#include <string.h>

#define TASK_STACK        8192       /* the input method converts on this stack */
#define POLL_MS           10u
#define CONNECT_POLL_MS   50u
#define DEBOUNCE_MS       250u        /* USB 2.0 7.1.7.3: wait for the connection to be stable */
#define RESET_HOLD_MS     30u         /* IDF's default; the spec asks >= 10 ms for a root port */
#define RESET_RECOVERY_MS 30u
#define ENABLE_WAIT_MS    200u
#define ADDR_RECOVERY_MS  10u         /* SET_ADDRESS: device may take up to 2 ms to respond at the new address */
#define KBD_ADDRESS       1
#define EP0_MPS_LOW       8u
#define EP0_MPS_FULL      64u
#define MAX_FAILS         20          /* consecutive failed polls before we assume the keyboard is gone */
#define REPEAT_DELAY_MS   400u
#define REPEAT_PERIOD_MS  50u

/* Standard requests (USB 2.0 table 9-4) and HID class requests (HID 1.11 section 7.2) */
#define REQ_GET_DESCRIPTOR   0x06
#define REQ_SET_ADDRESS      0x05
#define REQ_SET_CONFIG       0x09
#define HID_GET_REPORT       0x01
#define HID_SET_IDLE         0x0A
#define HID_SET_PROTOCOL     0x0B
#define DESC_DEVICE          1
#define DESC_CONFIG          2
#define DESC_INTERFACE       4
#define HID_CLASS            3
#define HID_SUBCLASS_BOOT    1
#define HID_PROTOCOL_KBD     1
#define HID_REPORT_INPUT     1
#define REPORT_BYTES         8


static const char *s_type = "none";
static keyboard_stats_t s_stats = { 0, -1, -1, -1, 0 };
static bool s_caps;
static uint8_t s_kbd_ep, s_kbd_interval;
static uint16_t s_kbd_mps;
static uint8_t s_msc_iface,s_msc_in,s_msc_out;
static uint16_t s_msc_imps,s_msc_omps;
static bool s_hub;
static uint16_t s_ep0_mps;
static usb_info_t device_info[USB_INFO_MAX];
static mutex_t info_lock=MUTEX_INIT;

size_t usb_native_snapshot(usb_info_t *out,size_t capacity)
{
    if (!out) return 0;
    size_t n=0;
    mutex_lock(&info_lock);
    for (unsigned i=1;i<USB_INFO_MAX && n<capacity;i++)
        if (device_info[i].present) out[n++]=device_info[i];
    mutex_unlock(&info_lock);
    return n;
}
static void info_clear(unsigned address)
{
    mutex_lock(&info_lock);
    if (address<USB_INFO_MAX) memset(&device_info[address],0,sizeof device_info[address]);
    else memset(device_info,0,sizeof device_info);
    mutex_unlock(&info_lock);
}
static void info_configured(unsigned address,bool configured)
{
    mutex_lock(&info_lock);
    device_info[address].configured=configured;
    mutex_unlock(&info_lock);
}
static void info_mount(unsigned address,int slot,bool mounted)
{
    mutex_lock(&info_lock);
    device_info[address].storage_slot=slot;
    device_info[address].mounted=mounted;
    mutex_unlock(&info_lock);
}

const char *usbkbd_type(void) { return s_type; }
void usbkbd_stats(keyboard_stats_t *out) { *out = s_stats; }

static void setup_packet(uint8_t s[8], uint8_t type, uint8_t req, uint16_t value, uint16_t index, uint16_t len)
{
    s[0] = type; s[1] = req;
    s[2] = (uint8_t)value; s[3] = (uint8_t)(value >> 8);
    s[4] = (uint8_t)index; s[5] = (uint8_t)(index >> 8);
    s[6] = (uint8_t)len;   s[7] = (uint8_t)(len >> 8);
}

static int ctrl(uint8_t type, uint8_t req, uint16_t value, uint16_t index, void *data, uint16_t len)
{
    uint8_t s[8];
    setup_packet(s, type, req, value, index, len);
    return hal_usbh_control(s, data, len);
}

/* ---- key decoding --------------------------------------------------------------- */

static void emit(uint8_t usage, uint8_t mods)
{
    char buf[8];
    hid_decode(usage, mods, s_caps, buf);
    if (buf[0]) { ime_feed(buf); s_stats.last_char = (unsigned char)buf[0]; }
}

static struct {
    uint8_t previous[8], held;
    uint64_t pressed, repeated;
} hub_keys;
static void hub_key_report(const uint8_t cur[8])
{
    uint64_t now=hal_time_now_us();
    for (unsigned i=2;i<8;i++) {
        uint8_t key=cur[i];
        if (key<HID_USAGE_FIRST) continue;
        bool old=false;
        for (unsigned j=2;j<8;j++) if (hub_keys.previous[j]==key) old=true;
        if (old) continue;
        s_stats.presses++;s_stats.last_x=key;
        if (key==HID_USAGE_CAPSLOCK) { s_caps=!s_caps;continue; }
        emit(key,cur[0]);hub_keys.held=key;
        hub_keys.pressed=hub_keys.repeated=now;
    }
    bool held=false;
    for (unsigned i=2;i<8;i++) if (cur[i]==hub_keys.held && cur[i]) held=true;
    if (!held) hub_keys.held=0;
    memcpy(hub_keys.previous,cur,8);
}
static void hub_key_repeat(void)
{
    uint64_t now=hal_time_now_us();
    if (hub_keys.held && now-hub_keys.pressed>REPEAT_DELAY_MS*1000u &&
        now-hub_keys.repeated>REPEAT_PERIOD_MS*1000u) {
        emit(hub_keys.held,hub_keys.previous[0]);hub_keys.repeated=now;
    }
}

/* ---- enumeration ---------------------------------------------------------------- */

static bool wait_enabled(void)
{
    for (uint32_t t = 0; t < ENABLE_WAIT_MS; t += 5) {
        if (hal_usbh_poll() == USBH_EVENT_ENABLED) return true;
        task_sleep(5);
    }
    return false;
}

/* Returns the HID boot keyboard's interface number, or -1. Sets *config to the configuration value. */
static int enumerate(usbh_speed_t speed, uint8_t address, int *config)
{
    uint8_t d[64];
    uint16_t mps0 = speed == USBH_SPEED_LOW ? EP0_MPS_LOW : EP0_MPS_FULL;
    bool low_via_hub = speed == USBH_SPEED_LOW && hal_usbh_speed() == USBH_SPEED_FULL;
    hal_usbh_prepare_device(0, low_via_hub);
    hal_usbh_prepare_device(address, low_via_hub);
    if (!hal_usbh_open_control(0, mps0)) { kprintf("usbkbd: no control channel\n"); return -1; }

    /* Read the first eight bytes before assuming endpoint-zero packet size.
     * Some hubs return only this prefix until EP0 has been configured. */
    int n = ctrl(0x80, REQ_GET_DESCRIPTOR, DESC_DEVICE << 8, 0, d, 8);
    if (n < 8) { kprintf("usbkbd: device descriptor prefix failed (%d)\n", n); return -1; }
    mps0 = d[7];
    if (mps0 != 8 && mps0 != 16 && mps0 != 32 && mps0 != 64) return -1;
    if (!hal_usbh_open_control(0, mps0)) return -1;
    n = ctrl(0x80, REQ_GET_DESCRIPTOR, DESC_DEVICE << 8, 0, d, 18);
    if (n < 18) { kprintf("usbkbd: device descriptor read failed (%d)\n", n); return -1; }
    uint16_t vid = (uint16_t)(d[8] | d[9] << 8), pid = (uint16_t)(d[10] | d[11] << 8);
    mps0 = d[7];
    if (mps0 != 8 && mps0 != 16 && mps0 != 32 && mps0 != 64) return -1;
    kprintf("usbkbd: device %04x:%04x, class %u, ep0 packet %u, %s speed\n", vid, pid, d[4], mps0,
            speed == USBH_SPEED_LOW ? "low" : speed == USBH_SPEED_FULL ? "full" : "high");

    s_ep0_mps = mps0;
    if (ctrl(0x00, REQ_SET_ADDRESS, address, 0, NULL, 0) < 0) { kprintf("usbkbd: SET_ADDRESS failed\n"); return -1; }
    task_sleep(ADDR_RECOVERY_MS);
    if (!hal_usbh_open_control(address, mps0)) return -1;
    mutex_lock(&info_lock);
    device_info[address]=(usb_info_t){.vid=vid,.pid=pid,.address=address,
        .parent=address==1?0:1,.port=address==1?0:address-1,.speed=speed,
        .device_class=d[4],.storage_slot=-1,.present=true};
    mutex_unlock(&info_lock);

    uint8_t cfg[1024];
    n = ctrl(0x80, REQ_GET_DESCRIPTOR, DESC_CONFIG << 8, 0, cfg, 9);
    if (n < 9) { kprintf("usbkbd: config descriptor read failed (%d)\n", n); return -1; }
    uint16_t total = (uint16_t)(cfg[2] | cfg[3] << 8);
    if (total > sizeof cfg) total = sizeof cfg;
    n = ctrl(0x80, REQ_GET_DESCRIPTOR, DESC_CONFIG << 8, 0, cfg, total);
    if (n < 9) { kprintf("usbkbd: full config descriptor read failed (%d)\n", n); return -1; }
    *config = cfg[5];

    int iface = -1, kind = 0, current = -1;
    s_kbd_ep = s_msc_in = s_msc_out = 0;
    s_hub = false;
    for (int i = 0; i + 2 <= n;) {
        int len = cfg[i];
        if (len < 2 || i + len > n) break;
        if (cfg[i+1] == DESC_INTERFACE && len >= 9) {
            current = cfg[i+2]; kind = 0;
            if (cfg[i+3] == 0 && cfg[i+5] == 9) s_hub = true;
            if (cfg[i+3] == 0 && cfg[i+5] == 3 && cfg[i+6] == 1 && cfg[i+7] == 1) {kind=1;}
            if (cfg[i+3] == 0 && cfg[i+5] == 8 && cfg[i+6] == 6 && cfg[i+7] == 0x50) {kind=2;s_msc_iface=current;}
        } else if (cfg[i+1] == 5 && len >= 7) {
            uint8_t ep=cfg[i+2],type=cfg[i+3]&3;
            uint16_t mps=(cfg[i+4]|cfg[i+5]<<8)&0x7ff;
            if(kind==1 && type==3 && (ep&0x80) && !s_kbd_ep) {
                iface=current;s_kbd_ep=ep;s_kbd_mps=mps;s_kbd_interval=cfg[i+6];
            }
            if(kind==2 && type==2) {
                if(ep&0x80){s_msc_in=ep;s_msc_imps=mps;}else{s_msc_out=ep;s_msc_omps=mps;}
            }
        }
        i += len;
    }
    mutex_lock(&info_lock);
    device_info[address].kinds=(s_hub?USB_INFO_HUB:0) |
        (s_kbd_ep?USB_INFO_KEYBOARD:0) |
        (s_msc_in && s_msc_out?USB_INFO_STORAGE:0);
    mutex_unlock(&info_lock);
    if(s_kbd_ep) {
        kprintf("usbkbd: interrupt IN ep=%02x mps=%u interval=%u\n", s_kbd_ep,s_kbd_mps,s_kbd_interval);
        return iface;
    }
    if(s_msc_in && s_msc_out)return -2;
    if(s_hub)return -3;
    if (vid==0x0bda && pid==0x8153 && native_ecm_probe(address,mps0,d[17],config)) {
        mutex_lock(&info_lock);
        device_info[address].kinds=USB_INFO_ETHERNET;
        mutex_unlock(&info_lock);
        return -4;
    }
    kprintf("usbkbd: device has no HID boot keyboard interface\n");
    return -1;
}

/* ---- the task ------------------------------------------------------------------- */

static int hub_request(uint16_t mps, uint8_t type, uint8_t req, uint16_t value,
                       uint16_t port, void *data, uint16_t len)
{
    mutex_lock(&usb_native_bus_lock);
    int rc = hal_usbh_open_control(1, mps) ? ctrl(type,req,value,port,data,len) : -1;
    mutex_unlock(&usb_native_bus_lock);
    return rc;
}
static bool hub_status(uint16_t mps, unsigned port, uint32_t *out)
{
    uint8_t s[4];
    if (hub_request(mps,0xa3,0,0,port,s,4)!=4) return false;
    *out=(uint32_t)s[0] | (uint32_t)s[1]<<8 | (uint32_t)s[2]<<16 | (uint32_t)s[3]<<24;
    return true;
}
/* Background topology polling must not queue behind a filesystem transaction:
 * the same task also has to drain Ethernet RX and service HID. Reset/enumeration
 * requests retain the blocking helper above. */
static bool hub_status_background(uint16_t mps,unsigned port,uint32_t *out)
{
    if (!mutex_trylock(&usb_native_bus_lock)) return false;
    uint8_t data[4];
    int n=hal_usbh_open_control(1,mps)?ctrl(0xa3,0,0,port,data,4):-1;
    mutex_unlock(&usb_native_bus_lock);
    if (n!=4) return false;
    *out=(uint32_t)data[0]|(uint32_t)data[1]<<8|(uint32_t)data[2]<<16|(uint32_t)data[3]<<24;
    return true;
}
static void hub_session(void)
{
    uint16_t hub_mps=s_ep0_mps;
    uint8_t desc[9];
    int n = hub_request(hub_mps,0xa0,REQ_GET_DESCRIPTOR,0x2900,0,desc,sizeof desc);
    if (n < 7 || desc[1] != 0x29 || !desc[2] || desc[2] > 7) {
        kprintf("usbhub: invalid descriptor (%d)\n", n);
        return;
    }
    unsigned ports = desc[2];
    kprintf("usbhub: ports=%u characteristics=%04x power-good=%u ms\n",
            ports, desc[3] | desc[4] << 8, desc[5] * 2u);
    for (unsigned port = 1; port <= ports; port++) {
        if (hub_request(hub_mps,0x23,3,8,port,NULL,0)<0) {
            kprintf("usbhub: port %u power failed\n", port);
            return;
        }
    }
    task_sleep(desc[5] * 2u + 100u);
    int slots[7];
    bool seen[7]={0}, used[NATIVE_MSC_SLOTS]={0};
    unsigned keyboard_port=0,keyboard_fails=0;
    unsigned ethernet_port=0;
    bool keyboard_pending=false;
    uint64_t status_at=0;
    memset(&hub_keys,0,sizeof hub_keys);
    for (unsigned i=0;i<ports;i++) slots[i]=-1;
    while (hal_usbh_connected()) {
        mutex_lock(&usb_native_bus_lock);
        hal_usbh_poll();
        uint8_t report[8];
        int key_result=USBH_PENDING;
        if (keyboard_port) {
            if (!keyboard_pending) keyboard_pending=hal_usbh_keyboard_start();
            key_result=keyboard_pending?hal_usbh_keyboard_poll(report):USBH_ERR_XFER;
            if (key_result!=USBH_PENDING) {
                keyboard_pending=false;
                if (key_result==8) keyboard_fails=0;
                else if (++keyboard_fails>=MAX_FAILS) {
                    hal_usbh_keyboard_close();keyboard_port=0;s_type="none";
                    memset(&hub_keys,0,sizeof hub_keys);
                    kprintf("usbkbd: hub keyboard stopped after transfer errors\n");
                }
            }
        }
        mutex_unlock(&usb_native_bus_lock);
        if (key_result==8) hub_key_report(report);
        else if (key_result!=USBH_PENDING) s_stats.i2c_errors++;
        if (keyboard_port) hub_key_repeat();
        if (ethernet_port) native_ecm_service();
        if (hal_time_now_us()-status_at<250000u) { task_sleep(5);continue; }
        status_at=hal_time_now_us();
        for (unsigned port = 1; port <= ports; port++) {
            uint32_t status;
            if (!hub_status_background(hub_mps,port,&status)) continue;
            bool connected=(status&1)!=0;
            if (!connected || (status&(1u<<16))) {
                info_clear(port+1);
                if (ethernet_port==port) { native_ecm_detach();ethernet_port=0; }
                if (keyboard_port==port) {
                    mutex_lock(&usb_native_bus_lock);
                    hal_usbh_keyboard_close();
                    mutex_unlock(&usb_native_bus_lock);
                    keyboard_port=0;keyboard_pending=false;s_type="none";
                    memset(&hub_keys,0,sizeof hub_keys);
                    kprintf("usbkbd: hub keyboard disconnected\n");
                }
                int slot=slots[port-1];
                if (slot>=0) {
                    /* Never acquire FAT while owning the bus lock: I/O takes
                     * FAT first, then the bus. Keep stale handles invalid. */
                    usb_fat_detach_slot(slot);
                    usbmsc_detach_slot(slot);
                    used[slot]=false; slots[port-1]=-1;
                    kprintf("usbhub: port %u storage removed\n",port);
                }
                seen[port-1]=false;
                if (status&(1u<<16)) hub_request(hub_mps,0x23,1,16,port,NULL,0);
            }
            if (!connected || seen[port-1]) continue;
            seen[port-1]=true;
            task_sleep(DEBOUNCE_MS);
            if (hub_request(hub_mps,0x23,3,4,port,NULL,0)<0) continue;
            bool enabled=false;
            for (unsigned t=0;t<500;t+=10) {
                task_sleep(10);
                if (!hub_status(hub_mps,port,&status)) break;
                if (!(status&0x10) && (status&2)) { enabled=true;break; }
            }
            if (!enabled) { kprintf("usbhub: port %u reset failed\n",port);continue; }
            hub_request(hub_mps,0x23,1,20,port,NULL,0);
            task_sleep(RESET_RECOVERY_MS);
            usbh_speed_t speed=(status&0x400)?USBH_SPEED_HIGH:
                               (status&0x200)?USBH_SPEED_LOW:USBH_SPEED_FULL;
            if (hal_usbh_speed()==USBH_SPEED_HIGH && speed!=USBH_SPEED_HIGH) {
                kprintf("usbhub: port %u requires split transfers (not enabled)\n",port);
                hub_request(hub_mps,0x23,1,1,port,NULL,0);
                continue;
            }
            uint8_t address=port+1;
            int config=0;
            mutex_lock(&usb_native_bus_lock);
            int iface=enumerate(speed,address,&config);
            bool configured=iface!=-1 && ctrl(0,REQ_SET_CONFIG,config,0,NULL,0)>=0;
            info_configured(address,configured);
            bool keyboard_ready=false;
            if (configured && iface>=0 && !keyboard_port &&
                ctrl(0x21,HID_SET_PROTOCOL,0,iface,NULL,0)>=0) {
                ctrl(0x21,HID_SET_IDLE,0,iface,NULL,0);
                keyboard_ready=hal_usbh_keyboard_open(address,s_kbd_ep,s_kbd_mps,s_kbd_interval);
            }
            uint8_t in=s_msc_in,out=s_msc_out,msc_iface=s_msc_iface;
            uint16_t imps=s_msc_imps,omps=s_msc_omps;
            mutex_unlock(&usb_native_bus_lock);
            if (!configured) {
                hub_request(hub_mps,0x23,1,1,port,NULL,0);
                continue;
            }
            if (iface==-4) {
                if (native_ecm_attach()) ethernet_port=port;
                continue;
            }
            if (iface!=-2) {
                if (keyboard_ready) {
                    keyboard_port=port;keyboard_pending=false;keyboard_fails=0;
                    s_type="usb-hub-hid";
                    kprintf("usbkbd: hub keyboard ready port=%u address=%u, separate DMA pipe\n",port,address);
                } else kprintf("usbhub: port %u unsupported or additional HID device\n",port);
                continue;
            }
            int slot=-1;
            for (unsigned i=0;i<NATIVE_MSC_SLOTS;i++)
                if (!used[i] && !usb_fat_slot_busy(i)) { slot=i;break; }
            if (slot<0) { kprintf("usbhub: no free storage slot\n");continue; }
            if (usbmsc_attach_slot(slot,address,msc_iface,in,imps,out,omps)) {
                slots[port-1]=slot;used[slot]=true;
                bool mounted=usb_fat_mount_slot(slot,usbmsc_device_slot(slot));
                info_mount(address,slot,mounted);
                kprintf("usbhub: port %u address=%u storage slot=%u\n",port,address,slot);
            }
        }
        task_sleep(5);
    }
    for (unsigned i=0;i<NATIVE_MSC_SLOTS;i++) if (used[i]) {
        usb_fat_detach_slot(i);
        usbmsc_detach_slot(i);
    }
    mutex_lock(&usb_native_bus_lock);
    hal_usbh_keyboard_close();
    mutex_unlock(&usb_native_bus_lock);
    s_type="none";
    native_ecm_detach();
}

static void keyboard_session(int iface)
{
    uint8_t prev[REPORT_BYTES] = { 0 }, cur[REPORT_BYTES];
    uint8_t held = 0;                                   /* the key that auto-repeats */
    uint64_t pressed_at = 0, last_repeat = 0;
    int fails = 0;
    bool pending = false;
    if (!hal_usbh_data_open(KBD_ADDRESS, s_kbd_ep, s_kbd_mps, true, s_kbd_interval)) {
        kprintf("usbkbd: cannot open interrupt endpoint\n"); return;
    }

    for (;;) {
        task_sleep(POLL_MS);
        usbh_event_t ev = hal_usbh_poll();
        if (ev == USBH_EVENT_DISCONNECT || ev == USBH_EVENT_DISABLED) { hal_usbh_data_close(); return; }

        if (!pending) {
            int rc = hal_usbh_data_start(cur, sizeof cur);
            if (rc < 0) { kprintf("usbkbd: submit failed %d\n", rc); break; }
            pending = true;
        }
        int n = hal_usbh_data_poll();
        if (n == USBH_PENDING) {
            uint64_t now = hal_time_now_us();
            if (held && now - pressed_at > REPEAT_DELAY_MS * 1000u && now - last_repeat > REPEAT_PERIOD_MS * 1000u) {
                emit(held, prev[0]); last_repeat = now;
            }
            continue; /* NAK/no new input is normal, not disconnection. */
        }
        pending = false;
        if (n < REPORT_BYTES) {
            s_stats.i2c_errors++;                       /* (field reused as "transfer errors") */
            if (++fails >= MAX_FAILS) { kprintf("usbkbd: interrupt reports failed, last=%d\n", n); break; }
            continue;
        }
        fails = 0;

        uint8_t mods = cur[0];
        uint64_t now = hal_time_now_us();
        for (int i = 2; i < REPORT_BYTES; i++) {
            uint8_t k = cur[i];
            if (k < HID_USAGE_FIRST) continue;              /* 0 = none, 1..3 = rollover / error codes */
            bool was_down = false;
            for (int j = 2; j < REPORT_BYTES; j++) if (prev[j] == k) was_down = true;
            if (was_down) continue;
            s_stats.presses++; s_stats.last_x = k;
            if (k == HID_USAGE_CAPSLOCK) { s_caps = !s_caps; continue; }
            emit(k, mods);
            held = k; pressed_at = last_repeat = now;
        }
        bool held_still = false;
        for (int i = 2; i < REPORT_BYTES; i++) if (cur[i] == held && held) held_still = true;
        if (!held_still) held = 0;
        else if (now - pressed_at > REPEAT_DELAY_MS * 1000u && now - last_repeat > REPEAT_PERIOD_MS * 1000u) {
            emit(held, mods);
            last_repeat = now;
        }
        memcpy(prev, cur, sizeof prev);
    }
    hal_usbh_data_close();
}

static void usbkbd_task(void *arg)
{
    (void)arg;
    kprintf("usb: native host, descriptor DMA, wait for host mode before FIFO setup\n");
    if (!hal_usbh_init()) { kprintf("usbkbd: USB host init failed\n"); return; }
    kprintf("usbkbd: waiting for a USB keyboard on the USB-A port\n");

    for (;;) {
        s_type = "none";
        info_clear(USB_INFO_MAX);
        /* IDF also enables its debounce lock on disconnect. Release it before
         * waiting again. Use the port level: another poller or a quick replug
         * can consume the connection edge before this task reaches the wait. */
        hal_usbh_debounce_done();
        while (!hal_usbh_connected()) {
            hal_usbh_poll();
            task_sleep(CONNECT_POLL_MS);
        }
        hal_usbh_poll();

        task_sleep(DEBOUNCE_MS);
        hal_usbh_debounce_done();
        if (!hal_usbh_connected()) continue;

        hal_usbh_reset(true);
        task_sleep(RESET_HOLD_MS);
        hal_usbh_reset(false);
        if (!wait_enabled()) { kprintf("usbkbd: port did not come up after reset\n"); continue; }
        task_sleep(RESET_RECOVERY_MS);

        int config = 0;
        int iface = enumerate(hal_usbh_speed(), KBD_ADDRESS, &config);
        if (iface == -2 && ctrl(0x00, REQ_SET_CONFIG, (uint16_t)config, 0, NULL, 0) >= 0) {
            info_configured(1,true);
            if (usbmsc_attach(s_msc_iface,s_msc_in,s_msc_imps,s_msc_out,s_msc_omps))
                info_mount(1,0,usb_fat_mount(usbmsc_device()));
            while(hal_usbh_connected()){hal_usbh_poll();task_sleep(CONNECT_POLL_MS);}
            usb_fat_detach();usbmsc_detach();
            kprintf("usbmsc: disconnected\n");
            /* Removal already observed; do not wait for a second unplug if
             * the device was reinserted during filesystem cleanup. */
            continue;
        } else if (iface == -3 && ctrl(0x00, REQ_SET_CONFIG, (uint16_t)config, 0, NULL, 0) >= 0) {
            info_configured(1,true);
            hub_session();
        } else if (iface == -4 && ctrl(0x00, REQ_SET_CONFIG, (uint16_t)config, 0, NULL, 0) >= 0) {
            info_configured(1,true);
            if (native_ecm_attach())
                while (hal_usbh_connected()) { native_ecm_service();task_sleep(5); }
            native_ecm_detach();
        } else if (iface >= 0 && ctrl(0x00, REQ_SET_CONFIG, (uint16_t)config, 0, NULL, 0) >= 0) {
            info_configured(1,true);
            int protocol_rc = ctrl(0x21, HID_SET_PROTOCOL, 0, (uint16_t)iface, NULL, 0);           /* 0 = boot protocol */
            ctrl(0x21, HID_SET_IDLE, 0, (uint16_t)iface, NULL, 0);               /* may STALL: not required */
            if (protocol_rc < 0) { kprintf("usbkbd: boot protocol rejected (%d)\n", protocol_rc); continue; }
            s_type = "usb-hid";
            kprintf("usbkbd: keyboard ready (interface %d)\n", iface);
            keyboard_session(iface);
            kprintf("usbkbd: keyboard gone\n");
        }
        /* wait for the cable to be unplugged (or the port to drop) before trying again */
        while (hal_usbh_connected()) { hal_usbh_poll(); task_sleep(CONNECT_POLL_MS); }
    }
}

void usbkbd_start(void)
{
    task_create_stack("usbkbd", usbkbd_task, NULL, TASK_PRIO_DEFAULT + 2, TASK_STACK);
}
