/* Native UVC YUY2 capture. USB class controls and payload framing; no RTOS host
 * stack. */
#include "native_uvc.h"
#include "hal_usbh_dma.h"
#include "native_msc.h"
#include "uvc_protocol.h"
#include <m5os/arch.h>
#include <m5os/hal_time.h>
#include <m5os/kmalloc.h>
#include <m5os/kprintf.h>
#include <m5os/task.h>
#include <string.h>
static uvc_status_t video;
static uvc_descriptor_t descriptors;
static mutex_t capture_lock = MUTEX_INIT;
static volatile bool attached;
static int request(uint8_t type, uint8_t req, uint16_t value, uint16_t index,
                   void *data, uint16_t len) {
  if (!hal_usbh_open_control(video.address, video.ep0))
    return -1;
  uint8_t s[8] = {type,  req,        value, value >> 8,
                  index, index >> 8, len,   len >> 8};
  return hal_usbh_control(s, data, len);
}
bool native_uvc_probe(uint8_t address, uint16_t ep0, const uint8_t *cfg,
                      size_t len, int *config) {
  if (!cfg || len < 9 || !config || !mutex_trylock(&capture_lock))
    return false;
  bool ok = false;
  uint8_t *full = NULL;
  size_t total = uvc_le16(cfg + 2);
  if (total < 9 || total > 4096)
    goto done;
  full = kmalloc_psram(total);
  if (!full)
    goto done;
  video = (uvc_status_t){.address = address, .ep0 = ep0};
  if (request(0x80, 6, 0x0200, 0, full, total) != (int)total)
    goto done;
  if (!uvc_descriptors(full, total, &descriptors))
    goto done;
  unsigned best = 0;
  uint32_t area = 0xffffffffu;
  for (unsigned i = 0; i < descriptors.nmodes; i++) {
    uvc_mode_t *m = &descriptors.modes[i];
    uint32_t a = (uint32_t)m->w * m->h;
    if (!(m->w & 1) && m->w <= 640 && m->h <= 480 && a < area) {
      best = i;
      area = a;
    }
  }
  if (area == 0xffffffffu || area > 640u * 480u)
    goto done;
  uvc_mode_t *m = &descriptors.modes[best];
  video.control = descriptors.control;
  video.stream = descriptors.stream;
  video.config = descriptors.config;
  video.version = descriptors.version;
  video.width = m->w;
  video.height = m->h;
  video.format = m->format;
  video.frame = m->index;
  video.frame_interval = m->interval;
  video.endpoint = descriptors.ep;
  video.present = true;
  attached = true;
  *config = video.config;
  ok = true;
  kprintf("uvc: address=%u UVC=%04x YUY2=%ux%u format=%u frame=%u alts=%u\n",
          address, video.version, video.width, video.height, video.format,
          video.frame, descriptors.nalts);
done:
  if (full)
    kfree(full);
  if (!ok) {
    video = (uvc_status_t){0};
    attached = false;
  }
  mutex_unlock(&capture_lock);
  return ok;
}
void native_uvc_configured(void) { video.configured = true; }
void native_uvc_detach(void) {
  attached = false;
  mutex_lock(&usb_native_bus_lock);
  hal_usbh_video_close();
  mutex_unlock(&usb_native_bus_lock);
  video.present = false;
  video.configured = false;
}
void native_uvc_status(uvc_status_t *out) {
  if (out)
    *out = video;
}
static uint8_t clamp(int n) { return n < 0 ? 0 : n > 255 ? 255 : n; }
static uint16_t rgb(int y, int u, int v) {
  int c = y - 16, d = u - 128, e = v - 128;
  uint8_t r = clamp((298 * c + 409 * e + 128) >> 8),
          g = clamp((298 * c - 100 * d - 208 * e + 128) >> 8),
          b = clamp((298 * c + 516 * d + 128) >> 8);
  return ((uint16_t)(r & 0xf8) << 8) | ((uint16_t)(g & 0xfc) << 3) | (b >> 3);
}
bool native_uvc_capture(uint16_t **pixels, int *width, int *height,
                        uint32_t timeout_ms) {
  if (!pixels || !width || !height || !mutex_trylock(&capture_lock))
    return false;
  *pixels = NULL;
  bool ok = false, opened = false, streaming = false;
  uint8_t *raw = NULL, *packet = NULL;
  uint16_t *out = NULL;
  task_t *self = task_current();
  uint8_t old_priority = self ? self->priority : 0;
  bool boosted = false;
  if (!attached || !video.configured || !timeout_ms || timeout_ms > 30000)
    goto done;
  size_t bytes = (size_t)video.width * video.height * 2;
  raw = kmalloc_psram(bytes);
  out = kmalloc_psram(bytes);
  packet = kmalloc_psram(3072);
  if (!raw || !out || !packet)
    goto done;
  uint8_t probe[48] = {0};
  uint16_t plen = video.version >= 0x150   ? 48
                  : video.version >= 0x110 ? 34
                                           : 26;
  probe[0] = 1;
  probe[2] = video.format;
  probe[3] = video.frame;
  uvc_put32(probe + 4, video.frame_interval);
  uvc_put32(probe + 18, bytes);
  uvc_put32(probe + 22, 1024);
  mutex_lock(&usb_native_bus_lock);
  int rc = request(1, 11, 0, video.stream, NULL, 0);
  if (rc >= 0)
    rc = request(0x21, 1, 0x0100, video.stream, probe, plen);
  if (rc >= 0)
    rc = request(0xa1, 0x81, 0x0100, video.stream, probe, plen);
  if (rc != plen) {
    mutex_unlock(&usb_native_bus_lock);
    kprintf("uvc: probe failed (%d)\n", rc);
    goto done;
  }
  uint32_t payload = uvc_le32(probe + 22), maxframe = uvc_le32(probe + 18);
  if (probe[2] != video.format || probe[3] != video.frame || maxframe < bytes ||
      !payload || payload > 3072) {
    mutex_unlock(&usb_native_bus_lock);
    kprintf("uvc: unsupported negotiated format/frame/payload %u\n", payload);
    goto done;
  }
  int chosen = -1;
  unsigned capacity = 0xffff;
  for (unsigned i = 0; i < descriptors.nalts; i++) {
    uvc_alt_t *a = &descriptors.alts[i];
    unsigned cap = (a->raw & 0x7ff) * (1 + ((a->raw >> 11) & 3));
    if (a->iface == video.stream && a->ep == video.endpoint && cap >= payload &&
        cap < capacity) {
      chosen = i;
      capacity = cap;
    }
  }
  if (chosen < 0) {
    mutex_unlock(&usb_native_bus_lock);
    kprintf("uvc: no alternate setting for payload %u\n", payload);
    goto done;
  }
  uvc_alt_t *a = &descriptors.alts[chosen];
  video.alt = a->alt;
  video.raw_mps = a->raw;
  video.interval = a->interval;
  video.payload = payload;
  rc = request(0x21, 1, 0x0200, video.stream, probe, plen);
  if (rc >= 0)
    rc = request(1, 11, a->alt, video.stream, NULL, 0);
  if (rc >= 0) {
    streaming = true;
    opened =
        hal_usbh_video_open(video.address, video.endpoint, a->raw, a->interval);
  }
  mutex_unlock(&usb_native_bus_lock);
  if (!opened) {
    kprintf("uvc: stream setup failed (%d)\n", rc);
    goto done;
  }
  kprintf("uvc: capture %ux%u payload=%u alt=%u capacity=%u\n", video.width,
          video.height, payload, a->alt, capacity);
  uvc_frame_t frame = {.data = raw, .capacity = bytes};
  bool pending = false;
  uint32_t irq = arch_irq_disable();
  if (self) {
    self->priority = TASK_PRIO_MAX - 1;
    boosted = true;
  }
  arch_irq_restore(irq);
  uint64_t burst = hal_time_now_us();
  uint64_t end = burst + (uint64_t)timeout_ms * 1000u;
  while (attached && hal_usbh_connected() && hal_time_now_us() < end) {
    if (hal_time_now_us() - burst > 100000u) {
      frame.bad = true;
      task_sleep(1);
      burst = hal_time_now_us();
    }
    mutex_lock(&usb_native_bus_lock);
    if (!attached) {
      mutex_unlock(&usb_native_bus_lock);
      break;
    }
    if (!pending)
      pending = hal_usbh_video_start(packet, capacity);
    int n = pending ? hal_usbh_video_poll() : USBH_ERR_XFER;
    mutex_unlock(&usb_native_bus_lock);
    if (n == USBH_PENDING)
      continue;
    pending = false;
    if (n < 0) {
      video.errors++;
      frame.bad = true;
      continue;
    }
    video.packets++;
    if (uvc_payload(&frame, packet, n)) {
      video.frames++;
      ok = true;
      break;
    }
    if (frame.bad)
      video.dropped++;
  }
done:
  if (boosted) {
    uint32_t flags = arch_irq_disable();
    self->priority = old_priority;
    arch_irq_restore(flags);
  }
  if (opened || streaming) {
    mutex_lock(&usb_native_bus_lock);
    hal_usbh_video_close();
    if (attached)
      request(1, 11, 0, video.stream, NULL, 0);
    mutex_unlock(&usb_native_bus_lock);
  }
  if (ok) {
    for (size_t i = 0; i < bytes; i += 4) {
      uint8_t y0 = raw[i], u = raw[i + 1], y1 = raw[i + 2], v = raw[i + 3];
      out[i / 2] = rgb(y0, u, v);
      out[i / 2 + 1] = rgb(y1, u, v);
    }
    *pixels = out;
    *width = video.width;
    *height = video.height;
    out = NULL;
  }
  if (raw)
    kfree(raw);
  if (packet)
    kfree(packet);
  if (out)
    kfree(out);
  mutex_unlock(&capture_lock);
  return ok;
}
