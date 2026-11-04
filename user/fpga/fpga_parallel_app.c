#include "fpga_parallel_app.h"
#include "../../drivers/fpga/fpga_parallel.h"
#include "../../drivers/fpga/fpga_tile_test.h"
#include "../../drivers/lcd/fbcon.h"
#include "../../third_party/carrier_link/link_protocol.h"
#include <m5os/display.h>
#include <m5os/task.h>
#if M5OS_MODULE_FPGA_GFX
#include "../../drivers/fpga/fpga_gfx.h"
#endif
#include <m5os/hal_console.h>
#include <m5os/kmalloc.h>
#include <m5os/kprintf.h>
#include <string.h>
static bool cancel(void *p) {
  (void)p;
  int k = hal_console_getc();
  return k == 3 || k == 27;
}
static bool frequency(const char *s, uint32_t *out, bool rx) {
  unsigned v = 0;
  if (!s || !*s)
    return false;
  for (; *s; s++) {
    if (*s < '0' || *s > '9' || v > (20000000u - (unsigned)(*s - '0')) / 10u)
      return false;
    v = v * 10 + (unsigned)(*s - '0');
  }
  if (v != 500000 && v != 1000000 && v != 2000000 && v != 5000000 &&
      !(rx && (v == 8000000 || v == 10000000 || v == 16000000 || v == 20000000)))
    return false;
  *out = v;
  return true;
}
static int usage(void) {
  kprintf("fpga --parlio dock-jumper-v0 HZ "
          "id|caps|test|gpu-test|gpu-verify|tile-test|tile-demo|batch-test "
          "[--gpio-rx | --rx-hz HZ]\nHZ: 500000,1000000,2000000,5000000; tagged finite DMA "
          "RX is default.\n--gpio-rx explicitly selects GPIO receive with DMA "
          "transmit. RX additionally accepts 8000000,10000000,16000000,20000000.\n"
          "--rx-hz changes only finite DMA RX; no automatic fallback/retry.\n");
  return 1;
}
int fpga_parallel_cmd_main(int argc, char **argv) {
  bool hybrid = false, rx_selected = false;
  uint32_t hz = 0, rx_hz = 0;
  if (argc < 5 || strcmp(argv[2], "dock-jumper-v0") ||
      !frequency(argv[3], &hz, false))
    return usage();
  for (int i = 5; i < argc; i++) {
    if (!strcmp(argv[i], "--gpio-rx") && !hybrid)
      hybrid = true;
    else if (!strcmp(argv[i], "--rx-hz") && !rx_selected && i + 1 < argc) {
      if (!frequency(argv[++i], &rx_hz, true))
        return usage();
      rx_selected = true;
    } else
      return usage();
  }
  if (hybrid && rx_selected)
    return usage();
  const char *cmd = argv[4];
  bool id = !strcmp(cmd, "id"), caps = !strcmp(cmd, "caps"),
       test = !strcmp(cmd, "test"), gpu = !strcmp(cmd, "gpu-test"),
       verify = !strcmp(cmd, "gpu-verify");
  bool batch = !strcmp(cmd, "batch-test");
  bool tile = !strcmp(cmd, "tile-test") || !strcmp(cmd, "tile-demo") || batch,
       demo = !strcmp(cmd, "tile-demo");
  if (!id && !caps && !test && !gpu && !verify && !tile)
    return usage();
#if !M5OS_MODULE_FPGA_GFX
  if (gpu || verify) {
    kprintf("fpga: fpga_gfx module required\n");
    return 1;
  }
#endif
  fpga_wire_workspace_t *ws = kmalloc_psram(sizeof *ws);
  fpga_tile_test_workspace_t *tw = tile ? kmalloc_psram(sizeof *tw) : NULL;
  fpga_tile_test_report_t tr = {0};
  uint16_t *pixels = (gpu || verify) ? kmalloc_psram(2048) : NULL;
  if (!ws || ((gpu || verify) && !pixels) || (tile && !tw)) {
    kfree(ws);
    kfree(pixels);
    kfree(tw);
    kprintf("fpga: no memory\n");
    return 1;
  }
  fpga_parallel_t bus = {0};
  fpga_wire_t wire = {0};
  uint8_t body[8] = {0};
  unsigned tests = 0, compared = 0, scenes = 0;
  int e = fpga_parallel_open(&bus, argv[2], hz);
  bus.cancel = cancel;
  if (!e && rx_selected && hal_parlio_set_rx_frequency(bus.native, rx_hz))
    e = FPGA_LINK_IO;
  if (!e)
    e = fpga_wire_init(&wire, &fpga_parallel_wire_ops, &bus, ws);
  /* Untagged capability negotiation deliberately uses GPIO RX. */
  if (!e)
    e = fpga_wire_caps(&wire, 100000);
  if (!e && caps)
    e = fpga_wire_tile_caps(&wire, 100000);
  if (!e && caps)
    e = fpga_wire_batch_caps(&wire, 100000);
  if (!e) {
    bus.native_rx = !hybrid;
    bus.tagged = !hybrid;
  }
  if (!e && tile)
    e = batch ? fpga_tile_batch_test_run(&wire, tw, &tr, !hybrid, 64, 30000000)
              : fpga_tile_test_run(&wire, tw, &tr, !hybrid, true, 30000000);
  if (!e && (id || test)) {
    unsigned count = test ? 513 : 1;
    for (unsigned i = 0; !e && i < count; i++) {
      uint8_t op = i == 0 ? LINK_ID : (i & 1) ? LINK_ECHO : LINK_INVERT;
      uint8_t v = (uint8_t)((i - 1) / 2);
      e = fpga_wire_command(&wire, op, i ? v : 0, i ? (uint8_t)(v ^ 255) : 0,
                            !hybrid, body, 8, 100000);
      if (!e)
        tests++;
    }
  }
#if M5OS_MODULE_FPGA_GFX
  if (!e && (gpu || verify)) {
    fpga_link_t link = {.ops = &fpga_parallel_link_ops, .context = &bus};
    e = verify ? fpga_gfx_verify(&link, pixels, &compared, &scenes)
               : fpga_gfx_diagnostic(&link, pixels, &compared);
    if (bus.error)
      e = bus.error;
  }
#endif
  if (e)
    bus.failed = true;
  fpga_parallel_close(&bus);
  if (!e && bus.error)
    e = bus.error;
  bool reset_required = e != 0;
  bool shown = false, paused = false;
  if (!e && demo) {
    if (!tr.image_valid)
      e = FPGA_LINK_DATA;
    else {
      fbcon_pause(true);
      paused = true;
      task_sleep(100);
      if (display_begin_frame()) {
        display_clear(COLOR_BLACK);
        display_draw_bitmap_scaled((DISPLAY_WIDTH - 512) / 2,
                                   (DISPLAY_HEIGHT - 512) / 2, 32, 32,
                                   tw->received_color, 16);
        shown = display_end_frame();
      }
      if (!shown)
        e = FPGA_LINK_IO;
    }
  }
  kprintf("{\"backend\":\"native-parlio\",\"rx\":\"%s\",\"hz\":%u,\"rx_hz\":%u,\"lcd_submitted\":%s,\"result\":"
          "\"%s\",\"sync_capable\":%s,\"tile_capable\":%s,\"batch_capable\":%s,"
          "\"test_transactions\":%u,\"compared_pixels\":%u,\"scenes_passed\":%"
          "u,\"data\":[%u,%u],\"tx_dma_completed\":%u,\"rx_dma_completed\":%u,"
          "\"tx_channel\":%d,\"rx_channel\":%d,\"phase_verified\":%s,\"parlio_"
          "raw\":%u,\"dma_raw\":%u,\"stage\":\"%s\",\"reset_required\":%s,"
          "\"last_rx_bytes\":%u,\"rx_phase_verified\":%s,\"quarantined\":%s,"
          "\"gate_gpio\":%d,\"etm_channel\":%d,\"gpio_task_channel\":%d,"
          "\"rx_gate_verified\":%s,\"rx_gate_closed\":%s,\"gate_event_seen\":%"
          "s,\"gate_task_seen\":%s,"
          "\"gate_ready_mask\":%u,\"gate_gpio_enable\":%u,\"gate_gpio_mux\":%u,"
          "\"gate_gpio_task\":%u,\"gate_etm_event\":%u,\"gate_etm_task\":%u}\n",
          hybrid ? "gpio" : "finite-dma", hz,
          hybrid ? 0 : bus.stats.rx_frequency_hz, shown ? "true" : "false", fpga_link_error(e),
          wire.sync_capable ? "true" : "false",
          wire.tile_capable ? "true" : "false",
          wire.batch_capable ? "true" : "false", tests, compared, scenes,
          body[4], body[5], bus.stats.tx_completed, bus.stats.rx_completed,
          bus.stats.tx_channel, bus.stats.rx_channel,
          bus.stats.tx_phase_verified ? "true" : "false", bus.stats.parlio_raw,
          bus.stats.dma_raw, bus.stats.stage ? bus.stats.stage : "not-opened",
          reset_required ? "true" : "false", bus.stats.last_rx_bytes,
          bus.stats.rx_phase_verified ? "true" : "false",
          bus.stats.dma_quarantined ? "true" : "false", bus.stats.gate_gpio,
          bus.stats.etm_channel, bus.stats.gpio_task_channel,
          bus.stats.rx_gate_verified ? "true" : "false",
          bus.stats.rx_gate_closed ? "true" : "false",
          bus.stats.gate_event_seen ? "true" : "false",
          bus.stats.gate_task_seen ? "true" : "false",
          bus.stats.gate_ready_mask, bus.stats.gate_gpio_enable,
          bus.stats.gate_gpio_mux, bus.stats.gate_gpio_task,
          bus.stats.gate_etm_event, bus.stats.gate_etm_task);
  if (tile)
    kprintf("{\"gpu_tile_test\":true,\"result\":\"%s\",\"scenes\":%u,"
            "\"pixels\":%u,\"scene\":%u,\"plane\":%u,\"pixel\":%u,\"expected\":"
            "%u,\"received\":%u,\"image_valid\":%s,\"mandelbrot_cycles\":%u,"
            "\"julia_cycles\":%u,\"elapsed_us\":%u}\n",
            fpga_link_error(e), tr.scenes, tr.pixels, tr.scene, tr.plane,
            tr.pixel, tr.expected, tr.received,
            tr.image_valid ? "true" : "false", tr.cycles[0], tr.cycles[1],
            (unsigned)tr.elapsed_us);
  if (shown) {
    kprintf("fpga: verified FPGA Julia tile displayed; Q/Esc/Enter closes\n");
    for (;;) {
      int k = hal_console_getc();
      if (k == 'q' || k == 'Q' || k == 27 || k == 3 || k == '\r' || k == '\n')
        break;
      task_sleep(20);
    }
  }
  if (paused)
    fbcon_pause(false);
  kfree(ws);
  kfree(pixels);
  kfree(tw);
  return e ? 1 : 0;
}
