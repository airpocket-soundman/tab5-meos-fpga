/* UART1 polling binding. The VM owner registry releases hardware on every VM
 * exit. */
#include "machine_meos.h"
#include "mphalport.h"
#include "py/mperrno.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include <m5os/hal_time.h>
#include <m5os/io_uart.h>
#define UART_READ_MAX 4096u
#define UART_WRITE_MAX 65536u
typedef struct {
  mp_obj_base_t base;
  io_owner_t owner;
  io_uart_handle_t handle;
  uint32_t timeout;
  bool active;
} meos_uart_t;
static meos_uart_t *uart_object(mp_obj_t obj) {
  if (!mp_obj_is_type(obj, &mp_machine_uart_type))
    mp_raise_TypeError(MP_ERROR_TEXT("expected UART"));
  return MP_OBJ_TO_PTR(obj);
}
static void uart_check(meos_uart_t *self) {
  if (!self->active)
    mp_raise_OSError(MP_EBADF);
  mp_machine_owner_check(self->owner);
}
static mp_int_t numeric(mp_obj_t obj) {
  if (!mp_obj_is_int(obj))
    mp_raise_TypeError(MP_ERROR_TEXT("expected integer"));
  return mp_obj_get_int(obj);
}
static mp_obj_t uart_make_new(const mp_obj_type_t *type, size_t n_args,
                              size_t n_kw, const mp_obj_t *all_args) {
  enum { ID, BAUD, TX, RX, TIMEOUT };
  static const mp_arg_t allowed[] = {
      {MP_QSTR_id, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_baudrate, MP_ARG_OBJ, {.u_obj = MP_OBJ_NEW_SMALL_INT(115200)}},
      {MP_QSTR_tx,
       MP_ARG_REQUIRED | MP_ARG_KW_ONLY | MP_ARG_OBJ,
       {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_rx,
       MP_ARG_REQUIRED | MP_ARG_KW_ONLY | MP_ARG_OBJ,
       {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_timeout,
       MP_ARG_KW_ONLY | MP_ARG_OBJ,
       {.u_obj = MP_OBJ_NEW_SMALL_INT(0)}},
  };
  mp_arg_val_t a[MP_ARRAY_SIZE(allowed)];
  mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed),
                            allowed, a);
  mp_int_t id = numeric(a[ID].u_obj), baud = numeric(a[BAUD].u_obj);
  mp_int_t tx = numeric(a[TX].u_obj), rx = numeric(a[RX].u_obj);
  mp_int_t timeout = numeric(a[TIMEOUT].u_obj);
  if (id != 1 || baud < 9600 || baud > 1000000 || tx < 0 || tx > 54 || rx < 0 ||
      rx > 54 || tx == rx || timeout < 0 || timeout > 60000)
    mp_raise_ValueError(MP_ERROR_TEXT("invalid UART1 configuration"));
  meos_uart_t *self = mp_obj_malloc(meos_uart_t, type);
  self->active = false;
  self->owner = mp_machine_owner();
  self->timeout = timeout;
  mp_machine_owner_check(self->owner);
  io_uart_config_t config = {
      .controller = 1, .tx_pin = tx, .rx_pin = rx, .baud = baud};
  mp_machine_error(io_uart_open(self->owner, &config, &self->handle));
  self->active = true;
  return MP_OBJ_FROM_PTR(self);
}
/* Native operations return partial progress before timeout. Slicing keeps
 * Ctrl-C responsive; all Python conversions and buffers are prepared before
 * I/O. */
static int uart_transfer(meos_uart_t *self, void *buffer, size_t length,
                         bool write) {
  uart_check(self);
  if (!length)
    return 0;
  uint64_t end = hal_time_now_us() + (uint64_t)self->timeout * 1000;
  size_t done = 0;
  do {
    uint64_t now = hal_time_now_us();
    uint32_t slice =
        self->timeout ? (now >= end ? 0 : (uint32_t)((end - now + 999) / 1000))
                      : 0;
    if (slice > 10)
      slice = 10;
    int rc = write
                 ? io_uart_write(self->owner, self->handle,
                                 (uint8_t *)buffer + done, length - done, slice)
                 : io_uart_read(self->owner, self->handle,
                                (uint8_t *)buffer + done, length - done, slice);
    if (rc > 0) {
      if ((size_t)rc > length - done)
        mp_raise_OSError(MP_EIO);
      done += rc;
    } else if (rc < 0 && rc != ERR_TIMEOUT) {
      if (done)
        return done;
      mp_machine_error(rc);
    }
    if (done == length || !self->timeout || hal_time_now_us() >= end)
      break;
    meos_mp_poll();
    uart_check(self);
  } while (true);
  return done;
}
static mp_obj_t uart_read(size_t n_args, const mp_obj_t *args) {
  meos_uart_t *self = uart_object(args[0]);
  mp_int_t count = n_args == 1 ? UART_READ_MAX : numeric(args[1]);
  if (count < 0 || count > UART_READ_MAX)
    mp_raise_ValueError(MP_ERROR_TEXT("read limit is 4096"));
  /* Allocate both the result object and backing before consuming UART data. */
  mp_obj_str_t *result = mp_obj_malloc(mp_obj_str_t, &mp_type_bytes);
  byte *data = m_new(byte, (size_t)count + 1);
  result->len = 0;
  result->hash = 0;
  result->data = data;
  data[0] = 0;
  int received = uart_transfer(self, data, count, false);
  if (!received && count) {
    return mp_const_none;
  }
  result->len = received;
  data[received] = 0;
  return MP_OBJ_FROM_PTR(result);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(uart_read_obj, 1, 2, uart_read);
static mp_obj_t uart_readinto(size_t n_args, const mp_obj_t *args) {
  meos_uart_t *self = uart_object(args[0]);
  mp_buffer_info_t buffer;
  mp_get_buffer_raise(args[1], &buffer, MP_BUFFER_WRITE);
  mp_int_t count = n_args == 3 ? numeric(args[2]) : (mp_int_t)buffer.len;
  if (count < 0 || count > UART_READ_MAX || (size_t)count > buffer.len)
    mp_raise_ValueError(MP_ERROR_TEXT("readinto limit is 4096"));
  int received = uart_transfer(self, buffer.buf, count, false);
  return !received && count ? mp_const_none : MP_OBJ_NEW_SMALL_INT(received);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(uart_readinto_obj, 2, 3,
                                           uart_readinto);
static mp_obj_t uart_write(mp_obj_t obj, mp_obj_t data) {
  meos_uart_t *self = uart_object(obj);
  mp_buffer_info_t buffer;
  mp_get_buffer_raise(data, &buffer, MP_BUFFER_READ);
  if (buffer.len > UART_WRITE_MAX)
    mp_raise_ValueError(MP_ERROR_TEXT("write limit is 65536"));
  int written = uart_transfer(self, buffer.buf, buffer.len, true);
  return !written && buffer.len ? mp_const_none : MP_OBJ_NEW_SMALL_INT(written);
}
static MP_DEFINE_CONST_FUN_OBJ_2(uart_write_obj, uart_write);
static mp_obj_t uart_flush(mp_obj_t obj) {
  meos_uart_t *self = uart_object(obj);
  uart_check(self);
  uint64_t end = hal_time_now_us() + (uint64_t)self->timeout * 1000;
  for (;;) {
    uint64_t now = hal_time_now_us();
    uint32_t slice =
        self->timeout ? (now >= end ? 0 : (uint32_t)((end - now + 999) / 1000))
                      : 0;
    if (slice > 10)
      slice = 10;
    int rc = io_uart_flush(self->owner, self->handle, slice);
    if (!rc)
      return mp_const_none;
    if (rc != ERR_TIMEOUT && rc != ERR_BUSY)
      mp_machine_error(rc);
    if (!self->timeout || hal_time_now_us() >= end) {
      mp_machine_error(rc);
      return mp_const_none;
    }
    meos_mp_poll();
    uart_check(self);
  }
}
static MP_DEFINE_CONST_FUN_OBJ_1(uart_flush_obj, uart_flush);
static mp_obj_t uart_deinit(mp_obj_t obj) {
  meos_uart_t *self = uart_object(obj);
  if (!self->active)
    return mp_const_none;
  mp_machine_owner_check(self->owner);
  int rc = io_uart_close(self->owner, self->handle);
  if (!rc || rc == ERR_BADF || rc == ERR_PERM)
    self->active = false;
  mp_machine_error(rc);
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(uart_deinit_obj, uart_deinit);
static const mp_rom_map_elem_t uart_locals_table[] = {
    {MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&uart_read_obj)},
    {MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&uart_readinto_obj)},
    {MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&uart_write_obj)},
    {MP_ROM_QSTR(MP_QSTR_flush), MP_ROM_PTR(&uart_flush_obj)},
    {MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&uart_deinit_obj)},
};
static MP_DEFINE_CONST_DICT(uart_locals, uart_locals_table);
MP_DEFINE_CONST_OBJ_TYPE(mp_machine_uart_type, MP_QSTR_UART, MP_TYPE_FLAG_NONE,
                         make_new, uart_make_new, locals_dict, &uart_locals);
