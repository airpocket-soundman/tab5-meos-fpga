#pragma once
#include <m5os/types.h>
typedef struct {
  bool present, configured, mjpeg;
  uint8_t address, control, stream, config, format, frame, endpoint, alt,
      interval;
  uint16_t ep0, version, width, height, raw_mps;
  uint32_t frame_interval, payload, packets, frames, dropped, errors;
  uint32_t jpeg_bad_header, jpeg_no_eoi, jpeg_bad_data; /* MJPEG frames rejected: header, missing end marker, entropy data */
  uint32_t err_flags, hdr_bad, short_eofs; /* packets with the UVC error bit / an unusable header; frames ending short */
  uint32_t eofs, bad_eofs; /* end-of-frame packets seen / of frames that arrived damaged or short */
  uint32_t last_bytes, last_wait_us, last_decode_us; /* last frame: size on the wire, time waiting for it, time decoding */
} uvc_status_t;
bool native_uvc_probe(uint8_t address, uint16_t ep0, const uint8_t *cfg,
                      size_t len, int *config);
void native_uvc_detach(void);
void native_uvc_configured(void);
void native_uvc_status(uvc_status_t *out);
/* Frame interval requested at the next capture, in 100 ns units (333333 = 30 fps); 0 = camera default. */
void native_uvc_set_interval(uint32_t interval);
/* Advertised YUY2 frame sizes (even widths can be captured). select_mode() fails while a capture is running. */
unsigned native_uvc_mode_count(void);
bool native_uvc_mode_get(unsigned index, uint16_t *width, uint16_t *height, uint32_t *interval);
bool native_uvc_select_mode(unsigned width, unsigned height, bool mjpeg);
/* MJPEG sizes the camera advertises (capturable when the mjpeg module is built in). */
unsigned native_uvc_mjpeg_mode_count(void);
bool native_uvc_mjpeg_mode_get(unsigned index, uint16_t *width, uint16_t *height, uint32_t *interval);
/* Streaming session: open once, then take frames as they arrive.  The session holds the capture lock (also
 * against select_mode and native_uvc_capture) until close, and must be closed by the task that opened it.
 * The buffer from stream_next() belongs to the session and is overwritten by the next call. */
bool native_uvc_stream_open(int *width, int *height);
/* width and height return the size of the frame.  MJPEG frames are decoded reduced by the largest half, quarter or
 * eighth that still fills max_width x max_height (0 = no reduction); YUY2 frames come back at the mode size. */
bool native_uvc_stream_next(const uint16_t **pixels, int *width, int *height, uint32_t timeout_ms, int max_width,
                            int max_height);
void native_uvc_stream_close(void);
/* Captures one complete YUY2 frame; returned RGB565 belongs to caller. */
bool native_uvc_capture(uint16_t **pixels, int *width, int *height,
                        uint32_t timeout_ms);
int uvc_cmd_main(int argc, char **argv);
