#include <m5os/device.h>
#include <m5os/app.h>
#include <m5os/errno.h>
#include <m5os/task.h>
#include <m5os/dev_buttons.h>
#include <m5os/display.h>
#include "../touch/ft6336.h"

#define TOUCH_POLL_MS   20u
#define BUTTON_COUNT    3

static volatile uint8_t  s_state;                /* single bytes: atomic to read */
static volatile uint16_t s_x, s_y;
static volatile uint8_t  s_pressed;

/* Kernel task that samples the touch panel and derives the virtual buttons. */
static void touchd(void *arg)
{
    (void)arg;
    for (;;) {
        uint16_t x, y;
        uint8_t mask = 0;
        if (touch_read(&x, &y) > 0) {
            s_x = x; s_y = y; s_pressed = 1;
            if (y >= DISPLAY_HEIGHT - BUTTONS_STRIP_H) {
                int zone = (x * BUTTON_COUNT) / DISPLAY_WIDTH;     /* 0,1,2 */
                if (zone >= BUTTON_COUNT) zone = BUTTON_COUNT - 1;
                mask = (uint8_t)(1u << zone);
            }
        } else {
            s_pressed = 0;
        }
        s_state = mask;
        task_sleep(TOUCH_POLL_MS);
    }
}

static int buttons_read(device_t *dev, void *buf, size_t len)
{
    (void)dev;
    if (len < 1) return ERR_INVAL;
    *(uint8_t *)buf = s_state;
    return 1;
}

static int buttons_ioctl(device_t *dev, uint32_t cmd, void *arg)
{
    (void)dev;
    if (cmd != BUTTONS_IOC_GET_TOUCH || !arg) return ERR_INVAL;
    buttons_touch_t *t = arg;
    t->x = s_x; t->y = s_y; t->pressed = s_pressed;
    return ERR_OK;
}

static const device_ops_t buttons_ops = { .read = buttons_read, .ioctl = buttons_ioctl };
static device_t buttons_device = { .name = "buttons", .ops = &buttons_ops, .required_perm = APP_PERM_INPUT };

void buttons_dev_register(void)
{
    if (touch_init()) {
        task_create("touchd", touchd, NULL, TASK_PRIO_DEFAULT + 1);
    }
    device_register(&buttons_device);
}
