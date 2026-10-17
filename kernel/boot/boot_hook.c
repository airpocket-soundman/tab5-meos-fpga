/*
 * boot_hook.c - where control leaves ESP-IDF and enters M5OS.
 *
 * Boot path (all verified in ESP-IDF v4.4.3 sources):
 *
 *   ROM -> 2nd-stage bootloader -> call_start_cpu0()   [esp_system/port/cpu_start.c]
 *        -> start_cpu0_default()                       [esp_system/startup.c]
 *             heap_caps_init(), clocks, flash/cache, PSRAM, ctors, ...
 *             disables the RTC boot watchdog
 *             esp_startup_start_app()   <-- normally freertos/port/xtensa/port.c:
 *                                           creates "main" task + vTaskStartScheduler()
 *
 * The linker flag --wrap=esp_startup_start_app (main/CMakeLists.txt) makes that
 * last call land in __wrap_esp_startup_start_app() below instead.  FreeRTOS is
 * linked (IDF components depend on it) but its scheduler never starts.
 *
 * We still run on the stack IDF set up for CPU0, with interrupts at the level
 * IDF left them.  kernel_main() will later switch to its own stacks.
 */
#include <m5os/types.h>
#include <m5os/kernel.h>

void __wrap_esp_startup_start_app(void)
{
    kernel_main();
    for (;;) { }            /* kernel_main() must not return */
}

/*
 * IDF's freertos port references app_main() from its (never started) main
 * task.  It is dead code under M5OS; trap loudly if it is ever reached.
 */
void app_main(void)
{
    kernel_panic("FreeRTOS main task ran: scheduler leak");
}
