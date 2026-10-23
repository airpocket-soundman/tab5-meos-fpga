/* Software buses share the owner/pin registry with shell GPIO. No Pin aliases.
 */
#include "machine_meos.h"
#include "py/mperrno.h"
#include "py/objlist.h"
#include "py/objstr.h"
#include <m5os/io_i2c.h>
#include <m5os/io_spi.h>
#include <string.h>
typedef struct {
  mp_obj_base_t base;
  io_owner_t owner;
  uint32_t handle;
  bool active;
} bus_t;
static bus_t *bus(mp_obj_t obj, bool spi) {
  if (!mp_obj_is_type(obj, spi ? &mp_machine_soft_spi_type
                               : &mp_machine_soft_i2c_type))
    mp_raise_TypeError(MP_ERROR_TEXT("wrong bus type"));
  bus_t *b = MP_OBJ_TO_PTR(obj);
  if (!b->active)
    mp_raise_OSError(MP_EBADF);
  mp_machine_owner_check(b->owner);
  return b;
}
static int gpio(mp_obj_t obj) {
  if (!mp_obj_is_int(obj))
    mp_raise_TypeError(MP_ERROR_TEXT("GPIO must be an integer"));
  mp_int_t pin = mp_obj_get_int(obj);
  if (pin < 0 || pin >= IO_PIN_COUNT)
    mp_raise_ValueError(MP_ERROR_TEXT("invalid GPIO"));
  return pin;
}
static size_t length(mp_obj_t obj) {
  mp_int_t n = mp_obj_get_int(obj);
  if (n < 0 || n > 4096)
    mp_raise_ValueError(MP_ERROR_TEXT("length must be 0..4096"));
  return n;
}
static void bound(size_t n) {
  if (n > 4096)
    mp_raise_ValueError(MP_ERROR_TEXT("buffer exceeds 4096 bytes"));
}
static int address(mp_obj_t obj) {
  mp_int_t a = mp_obj_get_int(obj);
  if (a < 0 || a > 127)
    mp_raise_ValueError(MP_ERROR_TEXT("address must be 7 bit"));
  return a;
}
/* Construct both object and backing store before touching the bus. Hash is
 * lazy. */
static mp_obj_t result_bytes(size_t n, byte **data) {
  mp_obj_str_t *o = mp_obj_malloc(mp_obj_str_t, &mp_type_bytes);
  byte *p = m_new(byte, n + 1);
  p[n] = 0;
  o->len = n;
  o->hash = 0;
  o->data = p;
  *data = p;
  return MP_OBJ_FROM_PTR(o);
}
static mp_obj_t bus_deinit(mp_obj_t self) {
  bool spi = mp_obj_is_type(self, &mp_machine_soft_spi_type);
  if (!spi && !mp_obj_is_type(self, &mp_machine_soft_i2c_type))
    mp_raise_TypeError(MP_ERROR_TEXT("wrong bus type"));
  bus_t *b = MP_OBJ_TO_PTR(self);
  if (!b->active)
    return mp_const_none;
  mp_machine_owner_check(b->owner);
  int rc = spi ? io_spi_close(b->owner, b->handle)
               : io_i2c_close(b->owner, b->handle);
  b->active = false;
  mp_machine_error(rc);
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(deinit_obj, bus_deinit);
static mp_obj_t i2c_make(const mp_obj_type_t *type, size_t n, size_t nk,
                         const mp_obj_t *a) {
  enum { SCL, SDA, FREQ, TIMEOUT };
  static const mp_arg_t allowed[] = {
      {MP_QSTR_scl,
       MP_ARG_KW_ONLY | MP_ARG_REQUIRED | MP_ARG_OBJ,
       {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_sda,
       MP_ARG_KW_ONLY | MP_ARG_REQUIRED | MP_ARG_OBJ,
       {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_freq, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 100000}},
      {MP_QSTR_timeout, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 10000}}};
  mp_arg_val_t v[4];
  mp_arg_parse_all_kw_array(n, nk, a, 4, allowed, v);
  if (v[TIMEOUT].u_int < 0 || v[TIMEOUT].u_int > 100000)
    mp_raise_ValueError(MP_ERROR_TEXT("invalid timeout"));
  io_i2c_config_t c = {.scl = gpio(v[SCL].u_obj),
                       .sda = gpio(v[SDA].u_obj),
                       .frequency_hz = v[FREQ].u_int,
                       .stretch_timeout_us = v[TIMEOUT].u_int};
  bus_t *b = mp_obj_malloc(bus_t, type);
  b->active = false;
  b->owner = mp_machine_owner();
  mp_machine_error(io_i2c_open(b->owner, &c, &b->handle));
  b->active = true;
  return MP_OBJ_FROM_PTR(b);
}
static void stop_check(mp_obj_t stop) {
  if (!mp_obj_is_true(stop))
    mp_raise_ValueError(MP_ERROR_TEXT("stop=False is unsupported"));
}
static mp_obj_t i2c_scan(mp_obj_t self) {
  bus_t *b = bus(self, false);
  mp_obj_t out = mp_obj_new_list(112, NULL);
  ((mp_obj_list_t *)MP_OBJ_TO_PTR(out))->len = 0;
  for (int a = 8; a < 120; a++) {
    bus(self, false);
    int rc = io_i2c_transfer(b->owner, b->handle, a, NULL, 0, NULL, 0);
    if (!rc)
      mp_obj_list_append(out, MP_OBJ_NEW_SMALL_INT(a));
    else if (rc != IO_ERR_NACK)
      mp_machine_error(rc);
    meos_mp_poll();
  }
  return out;
}
static MP_DEFINE_CONST_FUN_OBJ_1(scan_obj, i2c_scan);
static mp_obj_t i2c_read(size_t n, const mp_obj_t *a, mp_map_t *kw) {
  static const mp_arg_t allowed[] = {
      {MP_QSTR_stop, MP_ARG_OBJ, {.u_obj = mp_const_true}}};
  mp_arg_val_t v[1];
  mp_arg_parse_all(n - 3, a + 3, kw, 1, allowed, v);
  bus_t *b = bus(a[0], false);
  int addr = address(a[1]);
  size_t len = length(a[2]);
  stop_check(v[0].u_obj);
  byte *p;
  mp_obj_t out = result_bytes(len, &p);
  bus(a[0], false);
  if (len)
    mp_machine_error(
        io_i2c_transfer(b->owner, b->handle, addr, NULL, 0, p, len));
  return out;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(readfrom_obj, 3, i2c_read);
static mp_obj_t i2c_write(size_t n, const mp_obj_t *a, mp_map_t *kw) {
  static const mp_arg_t allowed[] = {
      {MP_QSTR_stop, MP_ARG_OBJ, {.u_obj = mp_const_true}}};
  mp_arg_val_t v[1];
  mp_arg_parse_all(n - 3, a + 3, kw, 1, allowed, v);
  bus_t *b = bus(a[0], false);
  int addr = address(a[1]);
  stop_check(v[0].u_obj);
  mp_buffer_info_t buf;
  mp_get_buffer_raise(a[2], &buf, MP_BUFFER_READ);
  bound(buf.len);
  bus(a[0], false);
  mp_machine_error(
      io_i2c_transfer(b->owner, b->handle, addr, buf.buf, buf.len, NULL, 0));
  return mp_obj_new_int(buf.len);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(writeto_obj, 3, i2c_write);
static mp_obj_t i2c_mem(size_t n, const mp_obj_t *a, mp_map_t *kw, bool write) {
  mp_arg_check_num(n, 0, 4, 4, false);
  bus_t *b = bus(a[0], false);
  int addr = address(a[1]);
  mp_int_t mem = mp_obj_get_int(a[2]);
  static const mp_arg_t allowed[] = {
      {MP_QSTR_addrsize, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 8}}};
  mp_arg_val_t v[1];
  mp_arg_parse_all(0, NULL, kw, 1, allowed, v);
  int bits = v[0].u_int;
  if ((bits != 8 && bits != 16) || mem < 0 || mem > (bits == 8 ? 255 : 65535))
    mp_raise_ValueError(MP_ERROR_TEXT("invalid memory address"));
  size_t prefix = bits / 8;
  byte head[2] = {(byte)(mem >> 8), (byte)mem};
  byte *tx = head + (2 - prefix), *rx = NULL;
  size_t tl = prefix, rl = 0;
  mp_obj_t out = mp_const_none;
  if (write) {
    mp_buffer_info_t buf;
    mp_get_buffer_raise(a[3], &buf, MP_BUFFER_READ);
    if (buf.len > 4096 - prefix)
      mp_raise_ValueError(MP_ERROR_TEXT("memory write exceeds 4096 bytes"));
    byte *p = m_new(byte, prefix + buf.len);
    memcpy(p, tx, prefix);
    memcpy(p + prefix, buf.buf, buf.len);
    tx = p;
    tl += buf.len;
  } else {
    rl = length(a[3]);
    if (!rl)
      mp_raise_ValueError(MP_ERROR_TEXT("memory read requires data"));
    out = result_bytes(rl, &rx);
  }
  bus(a[0], false);
  mp_machine_error(io_i2c_transfer(b->owner, b->handle, addr, tx, tl, rx, rl));
  return out;
}
static mp_obj_t read_mem(size_t n, const mp_obj_t *a, mp_map_t *k) {
  return i2c_mem(n, a, k, false);
}
static mp_obj_t write_mem(size_t n, const mp_obj_t *a, mp_map_t *k) {
  return i2c_mem(n, a, k, true);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(readmem_obj, 4, read_mem);
static MP_DEFINE_CONST_FUN_OBJ_KW(writemem_obj, 4, write_mem);
static mp_obj_t spi_make(const mp_obj_type_t *type, size_t n, size_t nk,
                         const mp_obj_t *a) {
  static const mp_arg_t allowed[] = {
      {MP_QSTR_sck,
       MP_ARG_KW_ONLY | MP_ARG_REQUIRED | MP_ARG_OBJ,
       {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_mosi,
       MP_ARG_KW_ONLY | MP_ARG_REQUIRED | MP_ARG_OBJ,
       {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_miso,
       MP_ARG_KW_ONLY | MP_ARG_REQUIRED | MP_ARG_OBJ,
       {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_cs,
       MP_ARG_KW_ONLY | MP_ARG_REQUIRED | MP_ARG_OBJ,
       {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_baudrate, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 100000}},
      {MP_QSTR_polarity, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0}},
      {MP_QSTR_phase, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0}}};
  mp_arg_val_t v[7];
  mp_arg_parse_all_kw_array(n, nk, a, 7, allowed, v);
  if (v[5].u_int < 0 || v[5].u_int > 1 || v[6].u_int < 0 || v[6].u_int > 1)
    mp_raise_ValueError(MP_ERROR_TEXT("invalid SPI mode"));
  io_spi_config_t c = {.sck = gpio(v[0].u_obj),
                       .mosi = gpio(v[1].u_obj),
                       .miso = gpio(v[2].u_obj),
                       .cs = gpio(v[3].u_obj),
                       .frequency_hz = v[4].u_int,
                       .mode = (v[5].u_int << 1) | v[6].u_int};
  bus_t *b = mp_obj_malloc(bus_t, type);
  b->active = false;
  b->owner = mp_machine_owner();
  mp_machine_error(io_spi_open(b->owner, &c, &b->handle));
  b->active = true;
  return MP_OBJ_FROM_PTR(b);
}
static mp_obj_t spi_write(mp_obj_t self, mp_obj_t data) {
  bus_t *b = bus(self, true);
  mp_buffer_info_t buf;
  mp_get_buffer_raise(data, &buf, MP_BUFFER_READ);
  bound(buf.len);
  bus(self, true);
  mp_machine_error(
      io_spi_transfer(b->owner, b->handle, buf.buf, NULL, buf.len));
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(spiwrite_obj, spi_write);
static mp_obj_t spi_read_common(size_t n, const mp_obj_t *a, mp_map_t *kw,
                                bool into) {
  static const mp_arg_t allowed[] = {
      {MP_QSTR_write, MP_ARG_OBJ, {.u_obj = MP_OBJ_NEW_SMALL_INT(0)}}};
  mp_arg_val_t v[1];
  mp_arg_parse_all(n - 2, a + 2, kw, 1, allowed, v);
  bus_t *b = bus(a[0], true);
  byte *rx;
  size_t len;
  mp_obj_t out = mp_const_none;
  mp_int_t fill = mp_obj_get_int(v[0].u_obj);
  if (fill < 0 || fill > 255)
    mp_raise_ValueError(MP_ERROR_TEXT("write must be a byte"));
  if (into) {
    mp_buffer_info_t buf;
    mp_get_buffer_raise(a[1], &buf, MP_BUFFER_WRITE);
    bound(buf.len);
    rx = buf.buf;
    len = buf.len;
  } else {
    len = length(a[1]);
    rx = NULL;
  }
  byte *tx = m_new(byte, len ? len : 1);
  memset(tx, fill, len);
  if (!into)
    out = result_bytes(len, &rx);
  bus(a[0], true);
  mp_machine_error(io_spi_transfer(b->owner, b->handle, tx, rx, len));
  return out;
}
static mp_obj_t spi_read(size_t n, const mp_obj_t *a, mp_map_t *k) {
  return spi_read_common(n, a, k, false);
}
static mp_obj_t spi_readinto(size_t n, const mp_obj_t *a, mp_map_t *k) {
  return spi_read_common(n, a, k, true);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(spiread_obj, 2, spi_read);
static MP_DEFINE_CONST_FUN_OBJ_KW(spireadinto_obj, 2, spi_readinto);
static mp_obj_t spi_both(mp_obj_t self, mp_obj_t tx, mp_obj_t rx) {
  bus_t *b = bus(self, true);
  mp_buffer_info_t t, r;
  mp_get_buffer_raise(tx, &t, MP_BUFFER_READ);
  mp_get_buffer_raise(rx, &r, MP_BUFFER_WRITE);
  bound(t.len);
  if (t.len != r.len)
    mp_raise_ValueError(MP_ERROR_TEXT("buffer lengths differ"));
  byte *snapshot = m_new(byte, t.len ? t.len : 1);
  memcpy(snapshot, t.buf, t.len);
  bus(self, true);
  mp_machine_error(
      io_spi_transfer(b->owner, b->handle, snapshot, r.buf, t.len));
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(spiboth_obj, spi_both);
static const mp_rom_map_elem_t i2c_locals[] = {
    {MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&deinit_obj)},
    {MP_ROM_QSTR(MP_QSTR_scan), MP_ROM_PTR(&scan_obj)},
    {MP_ROM_QSTR(MP_QSTR_readfrom), MP_ROM_PTR(&readfrom_obj)},
    {MP_ROM_QSTR(MP_QSTR_writeto), MP_ROM_PTR(&writeto_obj)},
    {MP_ROM_QSTR(MP_QSTR_readfrom_mem), MP_ROM_PTR(&readmem_obj)},
    {MP_ROM_QSTR(MP_QSTR_writeto_mem), MP_ROM_PTR(&writemem_obj)}};
static MP_DEFINE_CONST_DICT(i2c_dict, i2c_locals);
MP_DEFINE_CONST_OBJ_TYPE(mp_machine_soft_i2c_type, MP_QSTR_SoftI2C,
                         MP_TYPE_FLAG_NONE, make_new, i2c_make, locals_dict,
                         &i2c_dict);
static const mp_rom_map_elem_t spi_locals[] = {
    {MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&deinit_obj)},
    {MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&spiwrite_obj)},
    {MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&spiread_obj)},
    {MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&spireadinto_obj)},
    {MP_ROM_QSTR(MP_QSTR_write_readinto), MP_ROM_PTR(&spiboth_obj)}};
static MP_DEFINE_CONST_DICT(spi_dict, spi_locals);
MP_DEFINE_CONST_OBJ_TYPE(mp_machine_soft_spi_type, MP_QSTR_SoftSPI,
                         MP_TYPE_FLAG_NONE, make_new, spi_make, locals_dict,
                         &spi_dict);
