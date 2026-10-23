/* Real MicroPython parser/compiler/VM. Only board IO/time are mocked. */
#include "py/builtin.h"
#include "py/compile.h"
#include "py/cstack.h"
#include "py/gc.h"
#include "py/lexer.h"
#include "py/mperrno.h"
#include "py/reader.h"
#include "py/runtime.h"
#include <assert.h>
#include <m5os/io.h>
#include <m5os/io_i2c.h>
#include <m5os/io_spi.h>
#include <m5os/io_uart.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
_Static_assert(sizeof(void *) == 4 && sizeof(long) == 4 && sizeof(mp_int_t) == 4,
               "host must match 32-bit port widths");
static io_owner_t owner, next_owner;
static io_owner_t claimed[IO_PIN_COUNT];
static int levels[IO_PIN_COUNT];
static bool outputs[IO_PIN_COUNT];
static unsigned hw_calls, tx_bytes;
static uint64_t now_us;
static struct {
    bool active;
    io_owner_t owner;
    uint32_t handle;
    int pins[4];
    size_t count;
} buses[3];
static uint32_t next_handle;
static unsigned uart_rx;
void mp_machine_io_init(void);
void mp_machine_io_deinit(void);
int io_owner_create(const char *name, io_owner_t *out) {
    (void)name;
    if (owner)
        return ERR_BUSY;
    *out = owner = ++next_owner;
    return 0;
}
bool io_owner_valid(io_owner_t o) { return o && o == owner; }
int io_pins_claim(io_owner_t o, const int *p, size_t count, io_use_t use) {
    (void)use;
    if (!io_owner_valid(o))
        return ERR_PERM;
    for (size_t i = 0; i < count; i++) {
        if (p[i] < 0 || p[i] >= IO_PIN_COUNT)
            return ERR_INVAL;
        if (!((p[i] >= 16 && p[i] <= 19) || (p[i] >= 51 && p[i] <= 54)))
            return ERR_PERM;
        if (claimed[p[i]])
            return ERR_BUSY;
        for (size_t j = 0; j < i; j++)
            if (p[j] == p[i])
                return ERR_INVAL;
    }
    for (size_t i = 0; i < count; i++)
        claimed[p[i]] = o;
    return 0;
}
int io_pins_release(io_owner_t o, const int *p, size_t count) {
    for (size_t i = 0; i < count; i++)
        if (p[i] < 0 || p[i] >= IO_PIN_COUNT || claimed[p[i]] != o)
            return ERR_PERM;
    for (size_t i = 0; i < count; i++) {
        claimed[p[i]] = 0;
        outputs[p[i]] = false;
    }
    return 0;
}
int io_owner_close(io_owner_t o) {
    if (!io_owner_valid(o))
        return ERR_BADF;
    for (unsigned i = 0; i < IO_PIN_COUNT; i++)
        if (claimed[i] == o) {
            claimed[i] = 0;
            outputs[i] = false;
        }
    for (unsigned i = 0; i < 3; i++)
        if (buses[i].owner == o)
            buses[i].active = false;
    owner = 0;
    return 0;
}
static int pin_valid(io_owner_t o, int pin) {
    return io_owner_valid(o) && pin >= 0 && pin < IO_PIN_COUNT && claimed[pin] == o ? 0 : ERR_PERM;
}
int io_gpio_config(io_owner_t o, int pin, bool output, int initial, io_pull_t pull) {
    (void)pull;
    int r = pin_valid(o, pin);
    if (r)
        return r;
    hw_calls++;
    outputs[pin] = output;
    levels[pin] = initial;
    return 0;
}
int io_gpio_read(io_owner_t o, int pin, int *out) {
    int r = pin_valid(o, pin);
    if (!r) {
        hw_calls++;
        *out = levels[pin];
    }
    return r;
}
int io_gpio_write(io_owner_t o, int pin, int value) {
    int r = pin_valid(o, pin);
    if (!r && !outputs[pin])
        r = ERR_PERM;
    if (!r) {
        hw_calls++;
        levels[pin] = value;
    }
    return r;
}
int io_adc_read(io_owner_t o, int pin, unsigned atten, int *out) {
    int r = pin_valid(o, pin);
    if (!r) {
        hw_calls++;
        *out = 2048 + atten;
    }
    return r;
}
int io_pwm_set(io_owner_t o, int pin, uint32_t hz, unsigned duty, uint32_t *actual) {
    (void)duty;
    if (hz == 12345)
        return ERR_IO;
    int r = pin_valid(o, pin);
    if (!r) {
        hw_calls++;
        *actual = hz;
    }
    return r;
}
int io_pwm_stop(io_owner_t o, int pin) {
    int r = pin_valid(o, pin);
    if (!r)
        hw_calls++;
    return r;
}
static int bus_open(unsigned index, io_owner_t o, const int *pins, size_t count, io_use_t use,
                    uint32_t *h) {
    if (buses[index].active)
        return ERR_BUSY;
    int r = io_pins_claim(o, pins, count, use);
    if (r)
        return r;
    buses[index].active = true;
    buses[index].owner = o;
    buses[index].handle = *h = ++next_handle;
    buses[index].count = count;
    memcpy(buses[index].pins, pins, count * sizeof(int));
    hw_calls++;
    return 0;
}
static int bus_valid(unsigned i, io_owner_t o, uint32_t h) {
    return io_owner_valid(o) && buses[i].active && buses[i].owner == o && buses[i].handle == h
               ? 0
               : ERR_BADF;
}
static int bus_close(unsigned i, io_owner_t o, uint32_t h) {
    int r = bus_valid(i, o, h);
    if (r)
        return r;
    r = io_pins_release(o, buses[i].pins, buses[i].count);
    buses[i].active = false;
    return r;
}
int io_uart_open(io_owner_t o, const io_uart_config_t *c, io_uart_handle_t *h) {
    int p[] = {c->tx_pin, c->rx_pin};
    uart_rx = 0;
    return bus_open(0, o, p, 2, IO_USE_UART, h);
}
int io_uart_close(io_owner_t o, io_uart_handle_t h) { return bus_close(0, o, h); }
int io_uart_read(io_owner_t o, io_uart_handle_t h, void *p, size_t n, uint32_t timeout) {
    int r = bus_valid(0, o, h);
    if (r)
        return r;
    now_us += (uint64_t)timeout * 1000;
    hw_calls++;
    if (uart_rx >= 3)
        return timeout ? ERR_TIMEOUT : 0;
    if (n > 2)
        n = 2;
    if (n > 3 - uart_rx)
        n = 3 - uart_rx;
    memcpy(p, "abc" + uart_rx, n);
    uart_rx += n;
    return n;
}
int io_uart_write(io_owner_t o, io_uart_handle_t h, const void *p, size_t n, uint32_t timeout) {
    (void)p;
    int r = bus_valid(0, o, h);
    if (r)
        return r;
    now_us += (uint64_t)timeout * 1000;
    hw_calls++;
    if (n > 2)
        n = 2;
    tx_bytes += n;
    return n;
}
int io_uart_flush(io_owner_t o, io_uart_handle_t h, uint32_t timeout) {
    (void)timeout;
    return bus_valid(0, o, h);
}
int io_i2c_open(io_owner_t o, const io_i2c_config_t *c, uint32_t *h) {
    int p[] = {c->sda, c->scl};
    return bus_open(1, o, p, 2, IO_USE_I2C, h);
}
int io_i2c_close(io_owner_t o, uint32_t h) { return bus_close(1, o, h); }
int io_i2c_transfer(io_owner_t o, uint32_t h, uint8_t address, const uint8_t *tx, size_t nt,
                    uint8_t *rx, size_t nr) {
    (void)tx;
    (void)nt;
    int r = bus_valid(1, o, h);
    if (r)
        return r;
    hw_calls++;
    if (address != 0x42)
        return IO_ERR_NACK;
    for (size_t i = 0; i < nr; i++)
        rx[i] = (uint8_t)(address + i);
    return 0;
}
int io_i2c_cancel(io_owner_t o, uint32_t h) { return bus_valid(1, o, h); }
int io_spi_open(io_owner_t o, const io_spi_config_t *c, uint32_t *h) {
    int p[] = {c->sck, c->mosi, c->miso, c->cs};
    return bus_open(2, o, p, 4, IO_USE_SPI, h);
}
int io_spi_close(io_owner_t o, uint32_t h) { return bus_close(2, o, h); }
int io_spi_transfer(io_owner_t o, uint32_t h, const uint8_t *tx, uint8_t *rx, size_t n) {
    int r = bus_valid(2, o, h);
    if (r)
        return r;
    hw_calls++;
    for (size_t i = 0; i < n; i++)
        if (rx)
            rx[i] = (tx ? tx[i] : 0xff) ^ 0xff;
    return 0;
}
int io_spi_cancel(io_owner_t o, uint32_t h) { return bus_valid(2, o, h); }
uint64_t hal_time_now_us(void) { return now_us++; }
void task_sleep(uint32_t ms) { now_us += (uint64_t)ms * 1000; }
void task_yield(void) {}
void meos_mp_poll(void) { mp_handle_pending(true); }
void mp_hal_delay_ms(mp_uint_t ms) {
    task_sleep(ms);
    meos_mp_poll();
}
void mp_hal_delay_us(mp_uint_t us) { now_us += us; }
mp_uint_t mp_hal_ticks_ms(void) { return now_us / 1000; }
mp_uint_t mp_hal_ticks_us(void) { return now_us; }
mp_uint_t mp_hal_ticks_cpu(void) { return now_us; }
mp_uint_t mp_hal_stdout_tx_strn(const char *s, size_t n) { return fwrite(s, 1, n, stdout); }
void mp_hal_stdout_tx_str(const char *s) { fputs(s, stdout); }
void mp_hal_stdout_tx_strn_cooked(const char *s, size_t n) { fwrite(s, 1, n, stdout); }
int mp_hal_stdin_rx_chr(void) { return -1; }
void mp_hal_set_interrupt_char(int c) { (void)c; }
mp_import_stat_t mp_import_stat(const char *p) {
    (void)p;
    return MP_IMPORT_STAT_NO_EXIST;
}
mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    (void)filename;
    mp_raise_OSError(MP_ENOENT);
}
mp_obj_t mp_builtin_open(size_t n, const mp_obj_t *a, mp_map_t *kw) {
    (void)n;
    (void)a;
    (void)kw;
    mp_raise_OSError(MP_ENOENT);
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, mp_builtin_open);
void nlr_jump_fail(void *v) {
    (void)v;
    abort();
}
void gc_collect(void) {
    jmp_buf regs;
    (void)setjmp(regs);
    volatile uintptr_t here = 0;
    uintptr_t low = (uintptr_t)&here, top = (uintptr_t)MP_STATE_THREAD(stack_top);
    gc_collect_start();
    gc_collect_root((void **)&regs, sizeof(regs) / sizeof(uintptr_t));
    if (low < top)
        gc_collect_root((void **)low, (top - low) / sizeof(uintptr_t));
    gc_collect_end();
}
int main(int argc, char **argv) {
    if (argc < 2 || argc > 3)
        return 2;
    bool expect_exception = argc == 3 && !strcmp(argv[2], "--expect-runtime-error");
    bool exception_seen = false;
    FILE *f = fopen(argv[1], "rb");
    if (!f)
        return 3;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *source = malloc(size + 1);
    size_t loaded = fread(source, 1, size, f);
    if (loaded != (size_t)size)
        return 4;
    source[size] = 0;
    fclose(f);
    void *heap = malloc(512 * 1024);
    assert(heap);
    mp_cstack_init_with_sp_here(256 * 1024);
    gc_init(heap, (char *)heap + 512 * 1024);
    int result = 0;
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_init();
        mp_machine_io_init();
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, source, size, 0);
        qstr name = lex->source_name;
        mp_parse_tree_t tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_call_function_0(mp_compile(&tree, name, false));
        nlr_pop();
    } else {
        mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
        exception_seen = true;
        result =
            expect_exception && mp_obj_is_type(MP_OBJ_FROM_PTR(nlr.ret_val), &mp_type_RuntimeError)
                ? 0
                : 1;
    }
    if (expect_exception && !exception_seen)
        result = 1;
    mp_machine_io_deinit();
    assert(!owner);
    for (unsigned i = 0; i < IO_PIN_COUNT; i++)
        assert(!claimed[i]);
    for (unsigned i = 0; i < 3; i++)
        assert(!buses[i].active);
    mp_deinit();
    free(source);
    free(heap);
    printf("HOST IO mock: calls=%u accepted TX bytes=%u; all resources released\n", hw_calls,
           tx_bytes);
    return result;
}
