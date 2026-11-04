#include <m5os/device.h>
#include <m5os/board_config.h>

#if defined(M5OS_BOARD_TAB5)
void fat_automount_init(void);
void camera_dev_register(void);
#if M5OS_MODULE_FPGA_PARLIO
void fpga_dev_register(void);
#endif
#endif
void uart_dev_register(void);
void display_dev_register(void);
#if M5OS_HAS_TOUCH
void buttons_dev_register(void);
#endif

/* Registration order is also the order they appear in device_list(). */
void devices_init(void)
{
#if defined(M5OS_BOARD_TAB5)
    fat_automount_init();        /* block devices registered from here on get a FAT volume */
    camera_dev_register();       /* camera0 */
#if M5OS_MODULE_FPGA_PARLIO
    fpga_dev_register();         /* /dev/fpga: optional fixed GPU ioctl API */
#endif
#endif
    uart_dev_register();
    display_dev_register();
#if M5OS_HAS_TOUCH
    buttons_dev_register();      /* starts the touch polling task: needs sched_init() first */
#endif
    /* The Cardputer keyboard is not a device node: keyboard_start() feeds the console input. */
}
