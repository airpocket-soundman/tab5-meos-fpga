#pragma once
#include <m5os/types.h>
#define UVC_MAX_ALTS 16
#define UVC_MAX_MODES 24
typedef struct {
  uint8_t format, index;
  uint16_t w, h;
  uint32_t interval;
} uvc_mode_t;
typedef struct {
  uint8_t iface, alt, ep, interval;
  uint16_t raw;
} uvc_alt_t;
typedef struct {
  uint8_t config, control, stream, ep;
  uint16_t version;
  uvc_mode_t modes[UVC_MAX_MODES];
  uvc_alt_t alts[UVC_MAX_ALTS];
  unsigned nmodes, nalts;
} uvc_descriptor_t;
static uint16_t uvc_le16(const uint8_t *p) {
  return p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t uvc_le32(const uint8_t *p) {
  return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
static void uvc_put32(uint8_t *p, uint32_t n) {
  for (unsigned i = 0; i < 4; i++)
    p[i] = n >> (8 * i);
}
static bool uvc_descriptors(const uint8_t *b, size_t n, uvc_descriptor_t *v) {
  if (!b || !v || n < 9 || b[0] != 9 || b[1] != 2 || uvc_le16(b + 2) != n)
    return false;
  *v = (uvc_descriptor_t){.config = b[5]};
  unsigned cls = 0, sub = 0, iface = 0, alt = 0, format = 0;
  bool yuy2 = false;
  static const uint8_t guid[16] = {0x59, 0x55, 0x59, 0x32, 0, 0,    0x10, 0,
                                   0x80, 0,    0,    0xaa, 0, 0x38, 0x9b, 0x71};
  for (size_t i = 0; i < n;) {
    size_t l = b[i];
    if (l < 2 || l > n - i)
      return false;
    const uint8_t *d = b + i;
    if (d[1] == 4) {
      if (l < 9)
        return false;
      iface = d[2];
      alt = d[3];
      cls = d[5];
      sub = d[6];
      yuy2 = false;
      format = 0;
      if (cls == 14 && sub == 1 && !v->version)
        v->control = iface;
    } else if (d[1] == 0x24 && cls == 14) {
      if (l < 3)
        return false;
      if (sub == 1 && iface == v->control && d[2] == 1 && !v->version) {
        if (l < 13 || !d[11] || l < 12u + d[11])
          return false;
        v->version = uvc_le16(d + 3);
        v->stream = d[12];
      }
      if (sub == 2 && iface == v->stream && alt == 0) {
        if (d[2] == 1) {
          if (l < 13)
            return false;
          v->stream = iface;
          v->ep = d[6];
        }
        if (d[2] == 4) {
          if (l < 27)
            return false;
          format = d[3];
          yuy2 = true;
          for (unsigned k = 0; k < 16; k++)
            if (d[5 + k] != guid[k])
              yuy2 = false;
          if (d[21] != 16)
            yuy2 = false;
        }
        if (d[2] == 6) {
          if (l < 11)
            return false;
          format = d[3];
          yuy2 = false;
        }
        if (d[2] == 5 && yuy2) {
          if (l < 26)
            return false;
          uint16_t w = uvc_le16(d + 5), h = uvc_le16(d + 7);
          uint32_t interval = uvc_le32(d + 21);
          if (!w || !h || !interval)
            return false;
          if (v->nmodes < UVC_MAX_MODES)
            v->modes[v->nmodes++] = (uvc_mode_t){format, d[3], w, h, interval};
        }
      }
    } else if (d[1] == 5 && cls == 14 && sub == 2 && iface == v->stream &&
               alt) {
      if (l < 7)
        return false;
      uint16_t raw = uvc_le16(d + 4);
      if ((d[2] & 0x80) && (d[3] & 3) == 1 && (raw & 0x7ff) &&
          (raw & 0x7ff) <= 1024 && !(raw & 0xe000) && ((raw >> 11) & 3) < 3 &&
          d[6] && d[6] <= 16 && v->nalts < UVC_MAX_ALTS)
        v->alts[v->nalts++] = (uvc_alt_t){iface, alt, d[2], d[6], raw};
    }
    i += l;
  }
  return v->version && v->nmodes && v->nalts && v->ep;
}
/* Payload assembler: discard damaged/oversized/incomplete frames; FID starts a
 * new one. */
typedef struct {
  uint8_t *data;
  size_t capacity, used;
  uint8_t fid;
  bool started, bad, complete;
} uvc_frame_t;
static bool uvc_payload(uvc_frame_t *f, const uint8_t *p, size_t n) {
  if (!f || !p || !f->data || f->used > f->capacity)
    return false;
  if (n < 2) {
    if (n)
      f->bad = true;
    return false;
  }
  size_t h = p[0];
  if (h < 2 || h > n) {
    f->bad = true;
    return false;
  }
  size_t minimum = 2 + ((p[1] & 4) ? 4 : 0) + ((p[1] & 8) ? 6 : 0);
  if (h < minimum) {
    f->bad = true;
    return false;
  }
  uint8_t fid = p[1] & 1;
  if (!f->started || fid != f->fid) {
    f->started = true;
    f->fid = fid;
    f->used = 0;
    f->bad = false;
    f->complete = false;
  }
  if (p[1] & 0x40)
    f->bad = true;
  size_t bytes = n - h;
  if (bytes > f->capacity - f->used)
    f->bad = true;
  if (!f->bad && bytes) {
    for (size_t i = 0; i < bytes; i++)
      f->data[f->used + i] = p[h + i];
    f->used += bytes;
  }
  if (p[1] & 2) {
    f->complete = !f->bad && f->used == f->capacity;
    return f->complete;
  }
  return false;
}
