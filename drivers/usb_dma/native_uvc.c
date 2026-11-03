/* Native UVC YUY2 capture. USB class controls and payload framing; no RTOS host
 * stack. */
#include "native_uvc.h"
#include "hal_usbh_dma.h"
#include "native_msc.h"
#include "uvc_protocol.h"
#include <m5os/arch.h>
#include <m5os/devreg.h>
#include <m5os/hal_time.h>
#include <m5os/kmalloc.h>
#include <m5os/kprintf.h>
#include <m5os/task.h>
#include <string.h>
#ifndef M5OS_MODULE_UVC
#define M5OS_MODULE_UVC 1
#endif
#ifndef M5OS_MODULE_MJPEG
#define M5OS_MODULE_MJPEG 0
#endif
#if M5OS_MODULE_MJPEG
#include "../mjpeg/mjpeg.h"
#endif
#if M5OS_MODULE_UVC
static uvc_status_t video;
static uvc_descriptor_t descriptors;
static mutex_t capture_lock = MUTEX_INIT;
static volatile bool attached;
static bool capture_logged; /* the stream parameters are logged once per attach */
static uint32_t interval_override; /* 100 ns units; 0 = the camera's default frame interval */
void native_uvc_set_interval(uint32_t interval) { interval_override = interval; }
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
  capture_logged = false;
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
/* The YUY2 frame sizes the camera advertised; selecting one takes effect at the next capture. */
unsigned native_uvc_mode_count(void) { return attached ? descriptors.nmodes : 0; }
bool native_uvc_mode_get(unsigned index, uint16_t *width, uint16_t *height, uint32_t *interval) {
  if (!attached || index >= descriptors.nmodes)
    return false;
  if (width) *width = descriptors.modes[index].w;
  if (height) *height = descriptors.modes[index].h;
  if (interval) *interval = descriptors.modes[index].interval;
  return true;
}
unsigned native_uvc_mjpeg_mode_count(void) { return attached ? descriptors.njmodes : 0; }
bool native_uvc_mjpeg_mode_get(unsigned index, uint16_t *width, uint16_t *height, uint32_t *interval) {
  if (!attached || index >= descriptors.njmodes)
    return false;
  if (width) *width = descriptors.jmodes[index].w;
  if (height) *height = descriptors.jmodes[index].h;
  if (interval) *interval = descriptors.jmodes[index].interval;
  return true;
}
bool native_uvc_select_mode(unsigned width, unsigned height, bool mjpeg) {
  if (mjpeg && !M5OS_MODULE_MJPEG)
    return false;
  if (!mutex_trylock(&capture_lock))
    return false;
  bool ok = false;
  const uvc_mode_t *list = mjpeg ? descriptors.jmodes : descriptors.modes;
  unsigned count = mjpeg ? descriptors.njmodes : descriptors.nmodes;
  for (unsigned i = 0; attached && i < count; i++) {
    const uvc_mode_t *m = &list[i];
    if (m->w == width && m->h == height && !(m->w & 1)) {
      video.mjpeg = mjpeg;
      video.width = m->w;
      video.height = m->h;
      video.format = m->format;
      video.frame = m->index;
      video.frame_interval = m->interval;
      capture_logged = false;
      ok = true;
      break;
    }
  }
  mutex_unlock(&capture_lock);
  return ok;
}
void native_uvc_configured(void) {
  video.configured = true;
  devreg_remove("camera1");
  devreg_add(DEVREG_CAMERA, "camera1", "usb", 1, DEVREG_CAMERA_USB);
}
void native_uvc_detach(void) {
  devreg_remove("camera1");
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
/* A capture session keeps the stream open between frames: opening it (PROBE/COMMIT, alternate setting, pipe)
 * costs more than receiving a frame.  The session owns capture_lock from open to close. */
static struct {
  bool open, pending;
  uint8_t *raw, *packet[2]; /* two packet buffers: one is being received while the other is processed */
  int cur;
  uint16_t *out;
  size_t bytes;
  unsigned capacity;
  uvc_frame_t frame;
} sess;

static void session_free(void) {
  kfree(sess.raw);
  kfree(sess.packet[0]);
  kfree(sess.packet[1]);
  kfree(sess.out);
  memset(&sess, 0, sizeof sess);
}

bool native_uvc_stream_open(int *width, int *height) {
  if (!mutex_trylock(&capture_lock))
    return false;
  bool opened = false, streaming = false;
  memset(&sess, 0, sizeof sess);
  if (!attached || !video.configured)
    goto fail;
  size_t bytes = (size_t)video.width * video.height * 2; /* RGB565 output; also one raw YUY2 frame */
  size_t raw_bytes = bytes;
  if (video.mjpeg) { /* a JPEG frame is far smaller than raw; frames that do not fit are dropped */
    raw_bytes = bytes / 4;
    if (raw_bytes < 128u * 1024u) raw_bytes = 128u * 1024u;
    if (raw_bytes > 1536u * 1024u) raw_bytes = 1536u * 1024u;
  }
  sess.raw = kmalloc_psram(raw_bytes);
  sess.out = kmalloc_psram(bytes);
  sess.packet[0] = kmalloc_psram(3072);
  sess.packet[1] = kmalloc_psram(3072);
  if (!sess.raw || !sess.out || !sess.packet[0] || !sess.packet[1])
    goto fail;
  uint8_t probe[48] = {0};
  uint16_t plen = video.version >= 0x150   ? 48
                  : video.version >= 0x110 ? 34
                                           : 26;
  probe[0] = 1;
  probe[2] = video.format;
  probe[3] = video.frame;
  uvc_put32(probe + 4, interval_override ? interval_override : video.frame_interval);
  uvc_put32(probe + 18, raw_bytes);
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
    goto fail;
  }
  uint32_t payload = uvc_le32(probe + 22), maxframe = uvc_le32(probe + 18);
  if (probe[2] != video.format || probe[3] != video.frame || (!video.mjpeg && maxframe < bytes) ||
      !payload || payload > 3072) {
    mutex_unlock(&usb_native_bus_lock);
    kprintf("uvc: unsupported negotiated format/frame/payload %u\n", payload);
    goto fail;
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
    goto fail;
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
    opened = hal_usbh_video_open(video.address, video.endpoint, a->raw, a->interval);
  }
  mutex_unlock(&usb_native_bus_lock);
  if (!opened) {
    kprintf("uvc: stream setup failed (%d)\n", rc);
    goto fail_streaming;
  }
  if (!capture_logged) {
    capture_logged = true;
    kprintf("uvc: capture %s %ux%u payload=%u alt=%u capacity=%u\n", video.mjpeg ? "MJPEG" : "YUY2",
            video.width, video.height, payload, a->alt, capacity);
  }
  sess.bytes = bytes;
  sess.capacity = capacity;
  sess.frame = (uvc_frame_t){.data = sess.raw, .capacity = raw_bytes, .variable = video.mjpeg,
                             .ignore_error = video.mjpeg};
  sess.open = true;
  if (width) *width = video.width;
  if (height) *height = video.height;
  return true;
fail_streaming:
  if (streaming) {
    mutex_lock(&usb_native_bus_lock);
    if (attached)
      request(1, 11, 0, video.stream, NULL, 0);
    mutex_unlock(&usb_native_bus_lock);
  }
fail:
  session_free();
  mutex_unlock(&capture_lock);
  return false;
}

/* Waits for the next complete frame of an open session.  The returned RGB565 buffer belongs to the session and
 * is overwritten by the next call. */
/* Decodes the JPEG frame in sess.raw into sess.out, reduced to fit max_w x max_h. */
static bool finish_mjpeg(int max_w, int max_h, int *ow, int *oh) {
#if M5OS_MODULE_MJPEG
  mjpeg_info_t info;
  if (!mjpeg_info(sess.raw, sess.frame.used, &info)) {
    video.jpeg_bad_header++;
    return false;
  }
  /* The frame must end with EOI (cameras may pad after it): a frame that lost its tail is not decoded. */
  bool eoi = false;
  for (size_t i = sess.frame.used; i >= 2 && i + 16 > sess.frame.used && !eoi; i--)
    eoi = sess.raw[i - 2] == 0xFF && sess.raw[i - 1] == 0xD9;
  if (!eoi) {
    video.jpeg_no_eoi++;
    return false;
  }
  unsigned shift = (max_w > 0 && max_h > 0) ? mjpeg_shift_for(info.width, info.height, max_w, max_h) : 0;
  if (!mjpeg_decode_rgb565(sess.raw, sess.frame.used, shift, sess.out, sess.bytes / 2, ow, oh)) {
    video.jpeg_bad_data++;
    return false;
  }
  return true;
#else
  (void)max_w; (void)max_h; (void)ow; (void)oh;
  return false;
#endif
}

bool native_uvc_stream_next(const uint16_t **pixels, int *width, int *height, uint32_t timeout_ms, int max_width,
                            int max_height) {
  if (!pixels || !sess.open || !timeout_ms || timeout_ms > 30000)
    return false;
  *pixels = NULL;
  int out_w = video.width, out_h = video.height;
  uint64_t started_us = hal_time_now_us();
  bool ok = false, boundary = false;
  task_t *self = task_current();
  uint8_t old_priority = self ? self->priority : 0;
  bool boosted = false;
  /* The previous call left the stream mid-frame: start over, so a partial frame fails the length check. */
  sess.frame.started = false;
  sess.frame.bad = false;
  sess.frame.used = 0;
  uint32_t irq = arch_irq_disable();
  if (self) {
    self->priority = TASK_PRIO_MAX - 1;
    boosted = true;
  }
  arch_irq_restore(irq);
  uint64_t burst = hal_time_now_us();
  uint64_t end = burst + (uint64_t)timeout_ms * 1000u;
  while (attached && hal_usbh_connected() && hal_time_now_us() < end) {
    /* Sleeping loses the isochronous data of the next few microframes, so yield at a frame boundary (right after
     * an end-of-frame packet) once the burst is long, and mid-frame only as a last resort. */
    uint64_t busy = hal_time_now_us() - burst;
    if ((boundary && busy > 100000u) || busy > 1000000u) {
      if (!boundary)
        sess.frame.bad = true;
      task_sleep(1);
      burst = hal_time_now_us();
    }
    boundary = false;
    mutex_lock(&usb_native_bus_lock);
    if (!attached) {
      mutex_unlock(&usb_native_bus_lock);
      break;
    }
    if (!sess.pending)
      sess.pending = hal_usbh_video_start(sess.packet[sess.cur], sess.capacity);
    int n = sess.pending ? hal_usbh_video_poll() : USBH_ERR_XFER;
    uint8_t *packet = sess.packet[sess.cur];
    if (n != USBH_PENDING) {
      /* Isochronous data does not wait: arm the next microframe's transfer into the other buffer before this
       * packet is looked at, or whatever the camera sends meanwhile is lost (the camera then flags the frame). */
      sess.cur ^= 1;
      sess.pending = hal_usbh_video_start(sess.packet[sess.cur], sess.capacity);
    }
    mutex_unlock(&usb_native_bus_lock);
    if (n == USBH_PENDING)
      continue;
    if (n < 0) {
      video.errors++;
      sess.frame.bad = true;
      continue;
    }
    video.packets++;
    if (n >= 2 && (packet[1] & 0x40))
      video.err_flags++;
    if (n >= 2 && (packet[0] < 2 || packet[0] > n))
      video.hdr_bad++;
    bool got = uvc_payload(&sess.frame, packet, n);
    if (n >= 2 && (packet[1] & 2)) {
      boundary = true;
      video.eofs++;
      if (!got)
        video.bad_eofs++;
    }
    if (got) {
      uint64_t got_us = hal_time_now_us();
      video.last_wait_us = (uint32_t)(got_us - started_us);
      video.last_bytes = (uint32_t)sess.frame.used;
      if (video.mjpeg && !finish_mjpeg(max_width, max_height, &out_w, &out_h)) {
        video.dropped++; /* corrupt JPEG: wait for the next frame */
        sess.frame.complete = false;
        continue;
      }
      video.last_decode_us = (uint32_t)(hal_time_now_us() - got_us);
      video.frames++;
      ok = true;
      break;
    }
    if (sess.frame.bad)
      video.dropped++;
  }
  if (boosted) {
    uint32_t flags = arch_irq_disable();
    self->priority = old_priority;
    arch_irq_restore(flags);
  }
  if (ok) {
    if (!video.mjpeg) {
      const uint8_t *raw = sess.raw;
      uint16_t *out = sess.out;
      for (size_t i = 0; i < sess.bytes; i += 4) {
        uint8_t y0 = raw[i], u = raw[i + 1], y1 = raw[i + 2], v = raw[i + 3];
        out[i / 2] = rgb(y0, u, v);
        out[i / 2 + 1] = rgb(y1, u, v);
      }
    }
    *pixels = sess.out;
    if (width) *width = out_w;
    if (height) *height = out_h;
  }
  return ok;
}

void native_uvc_stream_close(void) {
  if (!sess.open)
    return;
  mutex_lock(&usb_native_bus_lock);
  hal_usbh_video_close();
  if (attached)
    request(1, 11, 0, video.stream, NULL, 0);
  mutex_unlock(&usb_native_bus_lock);
  session_free();
  mutex_unlock(&capture_lock);
}

/* One frame: open, receive, close.  The returned RGB565 buffer belongs to the caller (kfree). */
bool native_uvc_capture(uint16_t **pixels, int *width, int *height,
                        uint32_t timeout_ms) {
  if (!pixels || !width || !height)
    return false;
  *pixels = NULL;
  if (!native_uvc_stream_open(width, height))
    return false;
  const uint16_t *frame = NULL;
  bool ok = native_uvc_stream_next(&frame, width, height, timeout_ms, 0, 0);
  if (ok) {
    *pixels = sess.out; /* keep the converted frame; session_free() would release it */
    sess.out = NULL;
  }
  native_uvc_stream_close();
  return ok;
}
#else /* module left out: the USB host still enumerates the device, it just is not a camera */
bool native_uvc_probe(uint8_t address, uint16_t ep0, const uint8_t *cfg, size_t len, int *config) {
  (void)address; (void)ep0; (void)cfg; (void)len; (void)config;
  return false;
}
void native_uvc_detach(void) {}
void native_uvc_configured(void) {}
void native_uvc_status(uvc_status_t *out) {
  if (out)
    memset(out, 0, sizeof *out);
}
void native_uvc_set_interval(uint32_t interval) { (void)interval; }
unsigned native_uvc_mode_count(void) { return 0; }
bool native_uvc_mode_get(unsigned index, uint16_t *width, uint16_t *height, uint32_t *interval) {
  (void)index; (void)width; (void)height; (void)interval;
  return false;
}
unsigned native_uvc_mjpeg_mode_count(void) { return 0; }
bool native_uvc_mjpeg_mode_get(unsigned index, uint16_t *width, uint16_t *height, uint32_t *interval) {
  (void)index; (void)width; (void)height; (void)interval;
  return false;
}
bool native_uvc_select_mode(unsigned width, unsigned height, bool mjpeg) {
  (void)width; (void)height; (void)mjpeg;
  return false;
}
bool native_uvc_stream_open(int *width, int *height) {
  (void)width; (void)height;
  return false;
}
bool native_uvc_stream_next(const uint16_t **pixels, int *width, int *height, uint32_t timeout_ms, int max_width,
                            int max_height) {
  (void)pixels; (void)width; (void)height; (void)timeout_ms; (void)max_width; (void)max_height;
  return false;
}
void native_uvc_stream_close(void) {}
bool native_uvc_capture(uint16_t **pixels, int *width, int *height, uint32_t timeout_ms) {
  (void)pixels; (void)width; (void)height; (void)timeout_ms;
  return false;
}
#endif
