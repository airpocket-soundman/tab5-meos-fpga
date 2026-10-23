/* Single foreground VM. No FreeRTOS, libc stdio, or IDF Python port. */
#include "micropython_app.h"
#include "py/compile.h"
#include "py/cstack.h"
#include "py/gc.h"
#include "py/mphal.h"
#include "py/runtime.h"
#include "shared/runtime/pyexec.h"
#include <m5os/hal_time.h>
#include <m5os/kmalloc.h>
#include <m5os/kprintf.h>
#include <m5os/mutex.h>
#include <m5os/task.h>
#include <setjmp.h>
#include <string.h>

#define MP_HEAP_BYTES (512u * 1024u)
#define MP_TASK_STACK (32u * 1024u)
static volatile bool vm_live;
static volatile int vm_result;
static mutex_t vm_command_lock = MUTEX_INIT;
/* The native shell accepts at most 127 bytes per command line. */
static char argument[128];
static bool command_string;
static int interrupt_char = -1;
static unsigned poll_count;
static unsigned char input[128];
static unsigned input_read, input_write;
void mp_machine_io_init(void);
void mp_machine_io_deinit(void);
void meos_vfs_init(void);
int meos_vfs_deinit(void);
int meos_vfs_set_cwd(const char *path);

void mp_hal_set_interrupt_char(int c) { interrupt_char = c; }
void meos_mp_poll(void) {
  if (++poll_count % 32u)
    return;
  /* Bounded typeahead must never prevent a later Ctrl-C from being read. */
  for (unsigned checked = 0; checked < 64; checked++) {
    int c = kconsole_getc();
    if (c < 0)
      break;
    if (c == interrupt_char) {
      mp_sched_keyboard_interrupt();
      break;
    }
    if (input_write - input_read < sizeof(input))
      input[input_write++ % sizeof(input)] = (unsigned char)c;
  }
  mp_handle_pending(true);
}
int mp_hal_stdin_rx_chr(void) {
  for (;;) {
    if (input_read != input_write)
      return input[input_read++ % sizeof(input)];
    int c = kconsole_getc();
    if (c >= 0)
      return c;
    task_sleep(2);
  }
}
mp_uint_t mp_hal_stdout_tx_strn(const char *s, size_t n) {
  kconsole_write(s, n);
  return n;
}
void mp_hal_stdout_tx_str(const char *s) {
  mp_hal_stdout_tx_strn(s, strlen(s));
}
void mp_hal_stdout_tx_strn_cooked(const char *s, size_t n) {
  mp_hal_stdout_tx_strn(s, n);
}
void mp_hal_delay_ms(mp_uint_t ms) {
  while (ms) {
    mp_uint_t n = ms > 10 ? 10 : ms;
    task_sleep(n);
    ms -= n;
    poll_count = 31;
    meos_mp_poll();
  }
}
void mp_hal_delay_us(mp_uint_t us) {
  uint64_t end = hal_time_now_us() + us;
  while (hal_time_now_us() < end) {
  }
}
mp_uint_t mp_hal_ticks_ms(void) { return hal_time_now_us() / 1000u; }
mp_uint_t mp_hal_ticks_us(void) { return hal_time_now_us(); }
mp_uint_t mp_hal_ticks_cpu(void) { return hal_time_now_us(); }

void gc_collect(void) {
  /* setjmp spills callee-saved registers before scanning the live C stack. */
  jmp_buf registers;
  (void)setjmp(registers);
  volatile uintptr_t here = 0;
  uintptr_t low = (uintptr_t)&here, top = (uintptr_t)MP_STATE_THREAD(stack_top);
  gc_collect_start();
  size_t register_bytes = sizeof(registers);
  gc_collect_root((void **)&registers, register_bytes / sizeof(uintptr_t));
  if (low < top)
    gc_collect_root((void **)low, (top - low) / sizeof(uintptr_t));
  gc_collect_end();
}
void nlr_jump_fail(void *value) {
  (void)value;
  kputs("MicroPython fatal: uncaught NLR\n");
  for (;;)
    task_sleep(1000);
}

static void vm_task(void *unused) {
  (void)unused;
  void *heap = kmalloc_psram(MP_HEAP_BYTES);
  if (!heap) {
    kputs("python: cannot allocate GC heap\n");
    vm_live = false;
    return;
  }
  mp_cstack_init_with_sp_here(MP_TASK_STACK - 1024);
  gc_init(heap, (char *)heap + MP_HEAP_BYTES);
  input_read = input_write = poll_count = 0;
  interrupt_char = -1;
  nlr_buf_t nlr;
  if (nlr_push(&nlr) == 0) {
    mp_init();
    meos_vfs_init();
    mp_machine_io_init();
    if (argument[0]) {
      if (command_string) {
        mp_hal_set_interrupt_char(3);
        mp_lexer_t *lex = mp_lexer_new_from_str_len(
            MP_QSTR__lt_stdin_gt_, argument, strlen(argument), 0);
        qstr name = lex->source_name;
        mp_parse_tree_t tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_call_function_0(mp_compile(&tree, name, false));
      } else {
        char directory[96];
        size_t n = strlen(argument);
        if (n >= sizeof(directory))
          mp_raise_ValueError(MP_ERROR_TEXT("path too long"));
        memcpy(directory, argument, n + 1);
        char *slash = strrchr(directory, '/');
        if (slash == directory)
          slash[1] = 0;
        else
          *slash = 0;
        if (meos_vfs_set_cwd(directory) < 0)
          mp_raise_OSError(2);
        mp_obj_list_append(mp_sys_path,
                           mp_obj_new_str(directory, strlen(directory)));
        vm_result = pyexec_file(argument) == 1 ? 0 : 1;
      }
    } else {
      kputs("MicroPython: Ctrl-C interrupts; Ctrl-D exits to MeOS.\n");
      pyexec_mode_kind = PYEXEC_MODE_FRIENDLY_REPL;
      for (;;) {
        int result = pyexec_mode_kind == PYEXEC_MODE_RAW_REPL
                         ? pyexec_raw_repl()
                         : pyexec_friendly_repl();
        if (result & PYEXEC_FORCED_EXIT)
          break;
      }
      vm_result = 0;
    }
    if (command_string)
      vm_result = 0;
    nlr_pop();
  } else
    mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
  mp_hal_set_interrupt_char(-1);
  int close_error = meos_vfs_deinit();
  if (close_error < 0) {
    vm_result = 1;
    kprintf("python: file cleanup failed (%d)\n", close_error);
  }
  mp_machine_io_deinit();
  mp_deinit();
  kfree(heap);
  vm_live = false;
}
static int vm_command(int argc, char **argv) {
  if (vm_live) {
    kputs("python: VM already active\n");
    return 1;
  }
  if (argc > 3 || (argc == 3 && strcmp(argv[1], "-c")) ||
      (argc == 2 && !strcmp(argv[1], "-c"))) {
    kputs("usage: python [ABSOLUTE.py | -c \"SOURCE\"]\n");
    return 1;
  }
  command_string = argc == 3;
  const char *source = argc == 1 ? "" : argv[argc - 1];
  if (strlen(source) >= sizeof(argument) ||
      (!command_string && *source && *source != '/')) {
    kputs("python: invalid path or source too long\n");
    return 1;
  }
  strcpy(argument, source);
  vm_result = 1;
  vm_live = true;
  if (task_create_stack("micropython", vm_task, NULL, TASK_PRIO_DEFAULT,
                        MP_TASK_STACK) < 0) {
    vm_live = false;
    kputs("python: task creation failed\n");
    return 1;
  }
  while (vm_live)
    task_sleep(10);
  return vm_result;
}
int micropython_cmd_main(int argc, char **argv) {
  if (!mutex_trylock(&vm_command_lock)) {
    kputs("python: VM already active\n");
    return 1;
  }
  int result = vm_command(argc, argv);
  mutex_unlock(&vm_command_lock);
  return result;
}
