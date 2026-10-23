#pragma once
#include <m5os/types.h>
typedef struct {
  bool present, configured;
  uint8_t address, control, stream, config, format, frame, endpoint, alt,
      interval;
  uint16_t ep0, version, width, height, raw_mps;
  uint32_t frame_interval, payload, packets, frames, dropped, errors;
} uvc_status_t;
bool native_uvc_probe(uint8_t address, uint16_t ep0, const uint8_t *cfg,
                      size_t len, int *config);
void native_uvc_detach(void);
void native_uvc_configured(void);
void native_uvc_status(uvc_status_t *out);
/* Captures one complete YUY2 frame; returned RGB565 belongs to caller. */
bool native_uvc_capture(uint16_t **pixels, int *width, int *height,
                        uint32_t timeout_ms);
int uvc_cmd_main(int argc, char **argv);
