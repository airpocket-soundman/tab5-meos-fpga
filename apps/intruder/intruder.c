/* Test app: tries to use ANOTHER app's window.  Every attempt must be refused. */
#include "../apps.h"
#include <m5os/gui.h>
#include <m5os/errno.h>

volatile uint32_t app_intruder_target;
volatile uint32_t app_intruder_result;

void intruder_main(void *arg)
{
    (void)arg;
    window_t t = (window_t)app_intruder_target;
    uint32_t r = 0;
    if (window_fill_rect(t, 0, 0, 50, 50, 0xF800) == ERR_PERM) r |= 1;
    if (window_destroy(t) == ERR_PERM) r |= 2;
    if (window_focus(t) == ERR_PERM) r |= 4;
    app_intruder_result = r;
}
