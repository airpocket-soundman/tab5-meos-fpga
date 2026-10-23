#pragma once
#include <m5os/blockdev.h>
void usbmsc_service(void);
void usbmsc_detach(void);
blockdev_t *usbmsc_device(void);
