/*
 * The power daemon: watches the idle timer, puts the Tab5 into suspend and wakes it.  The decisions are in
 * kernel/lib/suspend_core.c (tested on the host); this file does the hardware.
 *
 * Suspend, in this order (the user sees the screen go dark first):
 *   backlight off -> panel off -> drawing frozen -> extension port 5 V off -> CPU clock down.
 * Wake, the reverse (the CPU first, so the rest is quick):
 *   CPU clock up -> 5 V back -> panel on -> drawing allowed -> backlight back to what it was.
 *
 * "Frozen" means display_set_suspended(): every drawing call is discarded, so the framebuffer keeps the last
 * picture and programs that keep drawing waste no time on it.  The console (fbcon) holds back its changes and
 * paints them on wake; full-screen programs are asked to repaint (DQK_REFRESH); WebAssembly apps are held in
 * their frame loop (suspend_wait_if_active).
 *
 * What stays untouched on purpose: the USB-A port's 5 V (the keyboard keeps its power), Wi-Fi, Tailscale and
 * the SSH sessions (their tasks keep running, just at the lower clock).
 */
#include <m5os/board_config.h>
#if M5OS_BOARD_TAB5
#include <m5os/suspend.h>
#include <m5os/suspend_core.h>
#include <m5os/display.h>
#include <m5os/board.h>
#include <m5os/hal_cpu.h>
#include <m5os/hal_time.h>
#include <m5os/kprintf.h>
#include <m5os/task.h>
#include <m5os/arch.h>
#include <m5os/io.h>
#include <string.h>

#define AWAKE_POLL_MS       200u        /* how often the idle timer is looked at */
#define SUSPENDED_POLL_MS   50u         /* 20 Hz: wake-up inputs */
#define SUSPEND_CPU_MHZ     90u         /* a quarter of the 360 MHz clock; the PLL keeps running */
#define IDLE_MIN_SECONDS    10u
#define IDLE_MAX_SECONDS    86400u
#define FULL_BRIGHTNESS     255u
#define TASK_STACK          6144
#define REQUEST_RENDER_WAIT_US 1000000u

static suspend_core_t s_core;
static uint8_t  s_saved_backlight;
static bool     s_ext5v_was_on;
static unsigned s_saved_mhz;
static uint32_t s_last_resume_ms;
static bool     s_started;
static bool     s_entering;
static uint64_t s_request_deadline;
static uint16_t s_inhibits[7];
/* UART focus is an application-owned input lease, independent of key drivers. */
extern bool kconsole_suspend_busy(void);

static int inhibit_index(uint32_t reason)
{
    for(unsigned i=0;i<7;i++)if(reason==(1u<<i))return (int)i;
    return -1;
}
bool suspend_inhibit_acquire(uint32_t reason)
{
    int i=inhibit_index(reason);if(i<0)return false;
    uint32_t irq=arch_irq_disable();
    bool ok=!s_entering&&!s_core.suspended&&s_inhibits[i]!=UINT16_MAX;
    if(ok)s_inhibits[i]++;
    arch_irq_restore(irq);return ok;
}
bool suspend_inhibit_release(uint32_t reason)
{
    int i=inhibit_index(reason);if(i<0)return false;
    uint32_t irq=arch_irq_disable();bool ok=s_inhibits[i]!=0;
    if(ok)s_inhibits[i]--;
    arch_irq_restore(irq);return ok;
}
uint32_t suspend_inhibit_reasons(void)
{
    uint32_t reasons=0,irq=arch_irq_disable();
    for(unsigned i=0;i<7;i++)if(s_inhibits[i])reasons|=1u<<i;
    arch_irq_restore(irq);
    /* These paths have no suspend clock/power quiesce contract yet. Block
     * even when apparently idle: a snapshot is not an acquisition barrier. */
#if M5OS_MODULE_FPGA || M5OS_MODULE_FPGA_PARLIO
    reasons|=SUSPEND_INHIBIT_FPGA;
#endif
#if M5OS_MODULE_CAMERA || M5OS_MODULE_UVC
    reasons|=SUSPEND_INHIBIT_CAMERA;
#endif
#if M5OS_USB_NATIVE_DMA
    reasons|=SUSPEND_INHIBIT_STORAGE;
#endif
    if(display_suspend_busy())reasons|=SUSPEND_INHIBIT_DISPLAY;
    if(kconsole_suspend_busy())reasons|=SUSPEND_INHIBIT_INPUT;
    for(int pin=0;pin<IO_PIN_COUNT;pin++){
        io_pin_info_t info;
        if(io_pin_info(pin,&info)||info.owner){reasons|=SUSPEND_INHIBIT_IO;break;}
    }
    return reasons;
}
static void report_busy(uint32_t reasons)
{
    kprintf("suspend: BUSY%s%s%s%s%s%s%s; no power/clock changes\n",
        reasons&SUSPEND_INHIBIT_IO?" IO lease":"",
        reasons&SUSPEND_INHIBIT_DISPLAY?" display lease/render":"",
        reasons&SUSPEND_INHIBIT_INPUT?" input focus":"",
        reasons&SUSPEND_INHIBIT_FPGA?" FPGA quiesce unsupported":"",
        reasons&SUSPEND_INHIBIT_CAMERA?" camera quiesce unsupported":"",
        reasons&SUSPEND_INHIBIT_STORAGE?" native USB/storage quiesce unsupported":"",
        reasons&SUSPEND_INHIBIT_OTHER?" other resource":"");
}
bool suspend_try_request(void)
{
    uint32_t reasons=suspend_inhibit_reasons();
    if(reasons){report_busy(reasons);return false;}
    uint32_t irq=arch_irq_disable();
    s_request_deadline=hal_time_now_us()+REQUEST_RENDER_WAIT_US;
    suspend_core_request_enter(&s_core);
    arch_irq_restore(irq);return true;
}

static uint64_t now_us(void) { return hal_time_now_us(); }

bool suspend_active(void) { return s_core.suspended; }

bool suspend_filter_input(void)
{
    if (!s_started) return false;
    uint32_t irq = arch_irq_disable();
    bool consumed = suspend_core_input(&s_core, now_us());
    arch_irq_restore(irq);
    return consumed;
}

void suspend_request(void)
{
    (void)suspend_try_request();
}

uint32_t suspend_poll_ms(uint32_t normal_ms)
{
    return (s_core.suspended && normal_ms < SUSPENDED_POLL_MS) ? SUSPENDED_POLL_MS : normal_ms;
}

void suspend_wait_if_active(void)
{
    while (s_core.suspended) task_sleep(SUSPENDED_POLL_MS);
}

static void serial_activity(void)               /* typing on the serial console counts, but is not swallowed */
{
    uint32_t irq = arch_irq_disable();
    suspend_core_wake(&s_core, now_us());
    arch_irq_restore(irq);
}

static void do_enter(void)
{
    uint32_t reasons=suspend_inhibit_reasons();
    uint32_t guard=arch_irq_disable();
    for(unsigned i=0;i<7;i++)if(s_inhibits[i])reasons|=1u<<i;
    bool manual=s_core.enter_requested;
    /* The command prompt itself schedules a console frame. Keep an accepted
     * manual request briefly pending while that frame drains; never bypass
     * the resource checks or defer other kinds of ownership changes. */
    bool retry=manual && reasons==SUSPEND_INHIBIT_DISPLAY && now_us()<s_request_deadline;
    if(reasons&&!retry){s_core.enter_requested=false;s_core.last_activity_us=now_us();s_request_deadline=0;}
    else if(!reasons){s_entering=true;s_request_deadline=0;}
    arch_irq_restore(guard);
    if(reasons){if(manual&&!retry)report_busy(reasons);return;} /* Auto-entry is inhibited too; do not spam the console. */
    kprintf("suspend: entering; any key, touch or movement wakes\n");
    s_saved_backlight = board_backlight_get();
    board_backlight(0);
    display_panel_power(false);
    display_set_suspended(true);
    board_ext5v_set(false, &s_ext5v_was_on);
    s_saved_mhz = hal_cpu_mhz();
    bool slowed = hal_cpu_set_mhz(SUSPEND_CPU_MHZ);
    kprintf("suspend: screen off, ext 5 V %s, CPU %u MHz%s\n", s_ext5v_was_on ? "off" : "was already off", hal_cpu_mhz(), slowed ? "" : " (clock change refused)");
    uint32_t irq = arch_irq_disable();
    suspend_core_entered(&s_core);
    arch_irq_restore(irq);
}

static void do_wake(void)
{
    uint64_t t0 = now_us();
    if (s_saved_mhz) hal_cpu_set_mhz(s_saved_mhz);
    if (s_ext5v_was_on) board_ext5v_set(true, NULL);
    display_panel_power(true);
    display_set_suspended(false);
    kconsole_request_refresh();                  /* full-screen programs repaint what they drew meanwhile */
    board_backlight(s_saved_backlight ? s_saved_backlight : FULL_BRIGHTNESS);
    uint32_t irq = arch_irq_disable();
    suspend_core_woke(&s_core, now_us());
    s_entering=false;
    arch_irq_restore(irq);
    s_last_resume_ms = (uint32_t)((now_us() - t0) / 1000u);
    kprintf("suspend: resumed in %u ms (CPU %u MHz)\n", (unsigned)s_last_resume_ms, hal_cpu_mhz());
}

static void powerd_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t irq = arch_irq_disable();
        suspend_action_t a = suspend_core_step(&s_core, now_us());
        arch_irq_restore(irq);
        if (a == SUSPEND_ENTER) do_enter();
        else if (a == SUSPEND_WAKE) do_wake();
        task_sleep(s_core.suspended ? SUSPENDED_POLL_MS : AWAKE_POLL_MS);
    }
}

void suspend_start(void)
{
    if (s_started) return;
#if M5OS_MODULE_FPGA || M5OS_MODULE_FPGA_PARLIO
    suspend_core_init(&s_core, now_us(), 0);
#else
    suspend_core_init(&s_core, now_us(), SUSPEND_DEFAULT_IDLE_SECONDS);
#endif
    kconsole_set_input_hook(serial_activity);
    s_started = true;
    task_create_stack("powerd", powerd_task, NULL, TASK_PRIO_DEFAULT + 2, TASK_STACK);
}

int suspend_command(int argc, char **argv)
{
    if (argc == 1) return suspend_try_request() ? 0 : 1;
    if (argc == 2 && !strcmp(argv[1], "status")) {
        kprintf("suspend: %s; auto-suspend after %u s idle (%u s left); CPU %u MHz; last resume %u ms\n",
                s_core.suspended ? "suspended" : "awake", (unsigned)s_core.idle_seconds,
                (unsigned)suspend_core_seconds_left(&s_core, now_us()), hal_cpu_mhz(),
                (unsigned)s_last_resume_ms);
        uint32_t reasons=suspend_inhibit_reasons();if(reasons)report_busy(reasons);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "idle")) {
        uint32_t secs = 0;
        if (!strcmp(argv[2], "off")) secs = 0;
        else {
            for (const char *p = argv[2]; *p; p++) { if (*p < '0' || *p > '9') { secs = 1; break; } secs = secs * 10 + (uint32_t)(*p - '0'); if (secs > IDLE_MAX_SECONDS) break; }
            if (secs < IDLE_MIN_SECONDS || secs > IDLE_MAX_SECONDS) { kprintf("sleep: seconds must be %u..%u, or off\n", IDLE_MIN_SECONDS, IDLE_MAX_SECONDS); return 1; }
        }
        uint32_t irq = arch_irq_disable();
        s_core.idle_seconds = secs;
        s_core.last_activity_us = now_us();      /* the new timeout counts from now */
        arch_irq_restore(irq);
        if (secs) kprintf("sleep: auto-suspend after %u s without key or touch\n", (unsigned)secs);
        else      kputs("sleep: auto-suspend off\n");
        return 0;
    }
    kputs("usage: sleep              suspend now\n       sleep status       state, timeout, CPU clock\n       sleep idle SECONDS|off   idle time before auto-suspend (10..86400)\n");
    return 1;
}
#endif
