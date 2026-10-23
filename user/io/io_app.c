#include "io_app.h"
#include <m5os/hal_time.h>
#include <m5os/io.h>
#include <m5os/io_i2c.h>
#include <m5os/io_spi.h>
#include <m5os/io_uart.h>
#include <m5os/kprintf.h>
#include <m5os/mutex.h>
#include <string.h>
static mutex_t cli_lock = MUTEX_INIT;
static io_owner_t owner;
static bool number(const char *s, uint32_t limit, uint32_t *out) {
  if (!s || !*s)
    return false;
  unsigned base = 10;
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s += 2;
    base = 16;
    if (!*s)
      return false;
  }
  uint32_t n = 0;
  for (; *s; s++) {
    unsigned d = *s >= '0' && *s <= '9'   ? *s - '0'
                 : *s >= 'a' && *s <= 'f' ? *s - 'a' + 10
                 : *s >= 'A' && *s <= 'F' ? *s - 'A' + 10
                                          : 99;
    if (d >= base || d > limit || n > (limit - d) / base)
      return false;
    n = n * base + d;
  }
  *out = n;
  return true;
}
static int hex_digit(char c) {
  return c >= '0' && c <= '9'   ? c - '0'
         : c >= 'a' && c <= 'f' ? c - 'a' + 10
         : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                : -1;
}
static int decode(const char *s, uint8_t *out, size_t cap) {
  size_t n = strlen(s);
  if (!n || (n & 1) || n / 2 > cap)
    return ERR_INVAL;
  for (size_t i = 0; i < n; i += 2) {
    int a = hex_digit(s[i]), b = hex_digit(s[i + 1]);
    if (a < 0 || b < 0)
      return ERR_INVAL;
    out[i / 2] = (a << 4) | b;
  }
  return n / 2;
}
static int result(int rc, void (*out)(const char *)) {
  char b[100];
  if (rc < 0) {
    ksnprintf(b, sizeof b, "io: %s (%d)\n", io_error_name(rc), rc);
    out(b);
    return 1;
  }
  out("ok\n");
  return 0;
}
static void bytes(const uint8_t *p, size_t n, void (*out)(const char *)) {
  char b[6];
  for (size_t i = 0; i < n; i++) {
    ksnprintf(b, sizeof b, "%02x%s", p[i], i + 1 == n ? "\n" : " ");
    out(b);
  }
  if (!n)
    out("(0 bytes)\n");
}
static int usage(void (*out)(const char *)) {
  out("gpio list [all] | mode PIN in [none|up|down] | mode PIN out [0|1]\n"
      "gpio read PIN | write PIN 0|1 | release PIN\n"
      "adc read PIN [ATTEN=0..3]  raw 12-bit, uncalibrated\n"
      "pwm set PIN HZ DUTY_PERMILLE | stop PIN\n"
      "i2c open SDA SCL HZ | scan HANDLE | read HANDLE ADDR N\n"
      "i2c write HANDLE ADDR HEX | xfer HANDLE ADDR HEX N | close HANDLE\n"
      "spi open SCK MOSI MISO CS HZ MODE | xfer HANDLE HEX | close HANDLE\n"
      "uart selftest (internal only) | open TX RX BAUD | read HANDLE N "
      "[TIMEOUT_MS]\n"
      "uart write HANDLE HEX [TIMEOUT_MS] | flush HANDLE [TIMEOUT_MS] | close "
      "HANDLE\n"
      "io caps | reset (close all Shell-owned resources)\n"
      "Numbers: decimal or 0xHEX. HEX data: contiguous pairs, maximum 128 "
      "bytes.\n");
  return 1;
}
static int command(int argc, char **v, void (*out)(const char *)) {
  if (argc < 2)
    return usage(out);
  char b[180];
  uint32_t a = 0, c = 0, d = 0, e = 0;
  int rc = ERR_INVAL;
  if (!strcmp(v[0], "io")) {
    if (argc == 2 && !strcmp(v[1], "help")) {
      usage(out);
      return 0;
    }
    if (argc == 2 && !strcmp(v[1], "caps")) {
      out("IO API 1: digital input/output/pulls; raw ADC; one PWM; "
          "SoftI2C/SoftSPI; UART1 8N1. DAC: absent.\n");
      return 0;
    }
    if (argc == 2 && !strcmp(v[1], "reset")) {
      if (!owner) {
        out("ok\n");
        return 0;
      }
      rc = io_owner_close(owner);
      if (!rc)
        owner = 0;
      return result(rc, out);
    }
    return usage(out);
  }
  if (argc == 2 && !strcmp(v[0], "uart") && !strcmp(v[1], "selftest"))
    return result(io_uart_selftest(), out);
  if (!strcmp(v[0], "gpio") && !strcmp(v[1], "list") &&
      (argc == 2 || (argc == 3 && !strcmp(v[2], "all")))) {
    out("PIN  ROLE                     STATE     OWNER      USE\n");
    for (int p = 0; p < IO_PIN_COUNT; p++) {
      io_pin_info_t x;
      io_pin_info(p, &x);
      if (argc == 2 && !x.exposed)
        continue;
      ksnprintf(b, sizeof b, "%2d   %-24s %-9s %-10s %u\n", p, x.label,
                x.reserved ? "reserved"
                : x.owner  ? "claimed"
                           : "available",
                x.owner ? x.owner_name : "-", x.use);
      out(b);
    }
    return 0;
  }
  if (!owner) {
    rc = io_owner_create("shell", &owner);
    if (rc)
      return result(rc, out);
  }
  if (!strcmp(v[0], "gpio")) {
    if (argc < 3 || !number(v[2], IO_PIN_COUNT - 1, &a))
      return usage(out);
    int pin = a;
    if (!strcmp(v[1], "mode") && argc >= 4 && argc <= 5) {
      bool output = !strcmp(v[3], "out");
      io_pull_t pull = IO_PULL_NONE;
      unsigned initial = 0;
      if (!output && strcmp(v[3], "in"))
        return usage(out);
      if (argc == 5) {
        if (output) {
          if (!number(v[4], 1, &c))
            return usage(out);
          initial = c;
        } else if (!strcmp(v[4], "up"))
          pull = IO_PULL_UP;
        else if (!strcmp(v[4], "down"))
          pull = IO_PULL_DOWN;
        else if (strcmp(v[4], "none"))
          return usage(out);
      }
      io_pin_info_t info;
      io_pin_info(pin, &info);
      bool claimed = false;
      if (info.owner != owner) {
        rc = io_pins_claim(owner, &pin, 1, IO_USE_DIGITAL);
        if (rc)
          return result(rc, out);
        claimed = true;
      }
      rc = io_gpio_config(owner, pin, output, initial, pull);
      if (rc && claimed)
        io_pins_release(owner, &pin, 1);
      return result(rc, out);
    }
    if (!strcmp(v[1], "read") && argc == 3) {
      int val;
      rc = io_gpio_read(owner, pin, &val);
      if (rc)
        return result(rc, out);
      ksnprintf(b, sizeof b, "%d\n", val);
      out(b);
      return 0;
    }
    if (!strcmp(v[1], "write") && argc == 4 && number(v[3], 1, &c))
      return result(io_gpio_write(owner, pin, c), out);
    if (!strcmp(v[1], "release") && argc == 3) {
      io_pin_info_t info;
      io_pin_info(pin, &info);
      if (info.use != IO_USE_DIGITAL)
        return result(ERR_PERM, out);
      return result(io_pins_release(owner, &pin, 1), out);
    }
    return usage(out);
  }
  if (!strcmp(v[0], "adc") && !strcmp(v[1], "read") &&
      (argc == 3 || argc == 4) && number(v[2], IO_PIN_COUNT - 1, &a) &&
      (argc == 3 || number(v[3], 3, &c))) {
    int pin = a, raw;
    rc = io_pins_claim(owner, &pin, 1, IO_USE_ADC);
    if (rc)
      return result(rc, out);
    rc = io_adc_read(owner, pin, c, &raw);
    io_pins_release(owner, &pin, 1);
    if (rc)
      return result(rc, out);
    ksnprintf(b, sizeof b,
              "GPIO%u raw=%d bits=12 attenuation_code=%u (uncalibrated)\n", a,
              raw, c);
    out(b);
    return 0;
  }
  if (!strcmp(v[0], "pwm") && argc >= 3 && number(v[2], IO_PIN_COUNT - 1, &a)) {
    int pin = a;
    if (argc == 3 && !strcmp(v[1], "stop")) {
      rc = io_pwm_stop(owner, pin);
      if (!rc)
        rc = io_pins_release(owner, &pin, 1);
      return result(rc, out);
    }
    if (argc == 5 && !strcmp(v[1], "set") && number(v[3], 40000000, &c) &&
        number(v[4], 1000, &d)) {
      io_pin_info_t info;
      io_pin_info(pin, &info);
      bool claimed = false;
      if (info.owner != owner || info.use != IO_USE_PWM) {
        rc = io_pins_claim(owner, &pin, 1, IO_USE_PWM);
        if (rc)
          return result(rc, out);
        claimed = true;
      }
      uint32_t actual;
      rc = io_pwm_set(owner, pin, c, d, &actual);
      if (rc && claimed)
        io_pins_release(owner, &pin, 1);
      if (rc)
        return result(rc, out);
      ksnprintf(b, sizeof b,
                "GPIO%u PWM actual_hz=%u requested_duty_permille=%u\n", a,
                actual, d);
      out(b);
      return 0;
    }
    return usage(out);
  }
  if (!strcmp(v[0], "i2c")) {
    if (argc == 5 && !strcmp(v[1], "open") && number(v[2], 54, &a) &&
        number(v[3], 54, &c) && number(v[4], 400000, &d)) {
      io_i2c_config_t cfg = {.sda = a, .scl = c, .frequency_hz = d};
      uint32_t h;
      rc = io_i2c_open(owner, &cfg, &h);
      if (rc)
        return result(rc, out);
      ksnprintf(b, sizeof b, "i2c handle=%u\n", h);
      out(b);
      return 0;
    }
    if (argc < 3 || !number(v[2], UINT32_MAX, &a))
      return usage(out);
    if (argc == 3 && !strcmp(v[1], "close"))
      return result(io_i2c_close(owner, a), out);
    if (argc == 3 && !strcmp(v[1], "scan")) {
      uint64_t end = hal_time_now_us() + 3000000;
      unsigned found = 0;
      for (unsigned addr = 8; addr < 120; addr++) {
        if (hal_time_now_us() > end)
          return result(ERR_TIMEOUT, out);
        rc = io_i2c_transfer(owner, a, addr, NULL, 0, NULL, 0);
        if (!rc) {
          ksnprintf(b, sizeof b, "0x%02x\n", addr);
          out(b);
          found++;
        } else if (rc != IO_ERR_NACK)
          return result(rc, out);
      }
      ksnprintf(b, sizeof b, "%u devices\n", found);
      out(b);
      return 0;
    }
    if (argc < 5 || !number(v[3], 127, &c))
      return usage(out);
    uint8_t tx[128], rx[128];
    int n;
    if (argc == 5 && !strcmp(v[1], "read") && number(v[4], 128, &d)) {
      rc = io_i2c_transfer(owner, a, c, NULL, 0, rx, d);
      if (rc)
        return result(rc, out);
      bytes(rx, d, out);
      return 0;
    }
    if (argc == 5 && !strcmp(v[1], "write") &&
        (n = decode(v[4], tx, sizeof tx)) >= 0)
      return result(io_i2c_transfer(owner, a, c, tx, n, NULL, 0), out);
    if (argc == 6 && !strcmp(v[1], "xfer") &&
        (n = decode(v[4], tx, sizeof tx)) >= 0 && number(v[5], 128, &d)) {
      rc = io_i2c_transfer(owner, a, c, tx, n, rx, d);
      if (rc)
        return result(rc, out);
      bytes(rx, d, out);
      return 0;
    }
    return usage(out);
  }
  if (!strcmp(v[0], "spi")) {
    if (argc == 8 && !strcmp(v[1], "open")) {
      uint32_t ps[4];
      for (unsigned i = 0; i < 4; i++)
        if (!number(v[i + 2], 54, &ps[i]))
          return usage(out);
      if (!number(v[6], 500000, &a) || !number(v[7], 3, &c))
        return usage(out);
      io_spi_config_t cfg = {.sck = ps[0],
                             .mosi = ps[1],
                             .miso = ps[2],
                             .cs = ps[3],
                             .frequency_hz = a,
                             .mode = c};
      uint32_t h;
      rc = io_spi_open(owner, &cfg, &h);
      if (rc)
        return result(rc, out);
      ksnprintf(b, sizeof b, "spi handle=%u\n", h);
      out(b);
      return 0;
    }
    if (argc < 3 || !number(v[2], UINT32_MAX, &a))
      return usage(out);
    if (argc == 3 && !strcmp(v[1], "close"))
      return result(io_spi_close(owner, a), out);
    if (argc == 4 && !strcmp(v[1], "xfer")) {
      uint8_t tx[128], rx[128];
      int n = decode(v[3], tx, sizeof tx);
      if (n < 0)
        return usage(out);
      rc = io_spi_transfer(owner, a, tx, rx, n);
      if (rc)
        return result(rc, out);
      bytes(rx, n, out);
      return 0;
    }
    return usage(out);
  }
  if (!strcmp(v[0], "uart")) {
    if (argc == 5 && !strcmp(v[1], "open") && number(v[2], 54, &a) &&
        number(v[3], 54, &c) && number(v[4], 1000000, &d)) {
      io_uart_config_t cfg = {
          .controller = 1, .tx_pin = a, .rx_pin = c, .baud = d};
      uint32_t h;
      rc = io_uart_open(owner, &cfg, &h);
      if (rc)
        return result(rc, out);
      ksnprintf(b, sizeof b, "uart handle=%u\n", h);
      out(b);
      return 0;
    }
    if (argc < 3 || !number(v[2], UINT32_MAX, &a))
      return usage(out);
    if (argc == 3 && !strcmp(v[1], "close"))
      return result(io_uart_close(owner, a), out);
    if ((argc == 3 || argc == 4) && !strcmp(v[1], "flush") &&
        (argc == 3 || number(v[3], 1000, &c)))
      return result(io_uart_flush(owner, a, argc == 3 ? 100 : c), out);
    if ((argc == 4 || argc == 5) && !strcmp(v[1], "read") &&
        number(v[3], 128, &d) && (argc == 4 || number(v[4], 1000, &e))) {
      uint8_t rx[128];
      rc = io_uart_read(owner, a, rx, d, e);
      if (rc < 0)
        return result(rc, out);
      bytes(rx, rc, out);
      return 0;
    }
    if ((argc == 4 || argc == 5) && !strcmp(v[1], "write") &&
        (argc == 4 || number(v[4], 1000, &e))) {
      uint8_t tx[128];
      int n = decode(v[3], tx, sizeof tx);
      if (n < 0)
        return usage(out);
      rc = io_uart_write(owner, a, tx, n, e);
      if (rc < 0)
        return result(rc, out);
      ksnprintf(b, sizeof b,
                "accepted=%d/%d bytes (use flush for wire completion)\n", rc,
                n);
      out(b);
      return rc == n ? 0 : 1;
    }
    return usage(out);
  }
  return usage(out);
}
int io_app_command(int argc, char **argv, void (*output)(const char *)) {
  if (!output || argc < 1 || !argv || !argv[0])
    return 1;
  for (int i = 0; i < argc; i++)
    if (!argv[i])
      return 1;
  mutex_lock(&cli_lock);
  io_reap_dead_owners();
  if (owner && !io_owner_valid(owner))
    owner = 0;
  int rc = command(argc, argv, output);
  mutex_unlock(&cli_lock);
  return rc;
}
