#include "uvc_app.h"
#if M5OS_BOARD_TAB5 && M5OS_USB_NATIVE_DMA
#include "../../drivers/lcd/fbcon.h"
#include "../../drivers/usb_dma/native_uvc.h"
#include "../camera/camera_app.h"
#include <m5os/display.h>
#include <m5os/kmalloc.h>
#include <m5os/kprintf.h>
#include <m5os/task.h>
#include <string.h>
int uvc_cmd_main(int argc, char **argv) {
  if (argc < 2 || !strcmp(argv[1], "status")) {
    uvc_status_t v;
    native_uvc_status(&v);
    kprintf("UVC: present=%u configured=%u address=%u version=%04x YUY2=%ux%u "
            "format=%u frame=%u\n",
            v.present, v.configured, v.address, v.version, v.width, v.height,
            v.format, v.frame);
    kprintf("UVC: packets=%u complete=%u dropped=%u errors=%u alt=%u "
            "payload=%u raw_mps=%04x\n",
            v.packets, v.frames, v.dropped, v.errors, v.alt, v.payload,
            v.raw_mps);
    return 0;
  }
  bool preview = !strcmp(argv[1], "preview"),
       save = !strcmp(argv[1], "capture") && argc == 3;
  if (!preview && !save) {
    kprintf("usage: uvc status | preview | capture PATH\n");
    return 1;
  }
  uint16_t *p = NULL;
  int w = 0, h = 0;
  if (!native_uvc_capture(&p, &w, &h, 10000)) {
    kprintf("uvc: no complete frame captured\n");
    return 1;
  }
  bool ok = true;
  if (save) {
    ok = camera_image_save_rgb565(argv[2], p, w, h);
    kprintf("uvc: %s %ux%u BMP %s\n", ok ? "saved" : "save failed", w, h,
            argv[2]);
  }
  if (preview) {
    bool paused = fbcon_is_paused();
    fbcon_pause(true);
    task_sleep(80);
    bool batch = display_begin_frame();
    display_clear(COLOR_BLACK);
    camera_draw_rgb565_fit(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, p, w, h);
    if (batch)
      display_end_frame();
    task_sleep(1500);
    fbcon_pause(paused);
  }
  kfree(p);
  return ok ? 0 : 1;
}
#else
int uvc_cmd_main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  return 1;
}
#endif
