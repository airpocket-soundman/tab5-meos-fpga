#pragma once
#include <m5os/blockdev.h>
bool usbmsc_attach(uint8_t iface, uint8_t in, uint16_t in_mps, uint8_t out, uint16_t out_mps);
void usbmsc_detach(void);
blockdev_t *usbmsc_device(void);
