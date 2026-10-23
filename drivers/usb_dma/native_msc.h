#pragma once
#include <m5os/blockdev.h>
#include <m5os/mutex.h>
extern mutex_t usb_native_bus_lock;
#define NATIVE_MSC_SLOTS 3
bool usbmsc_attach_slot(unsigned slot, uint8_t address, uint8_t iface, uint8_t in,
                        uint16_t in_mps, uint8_t out, uint16_t out_mps);
void usbmsc_detach_slot(unsigned slot);
blockdev_t *usbmsc_device_slot(unsigned slot);
bool usbmsc_attach(uint8_t iface, uint8_t in, uint16_t in_mps, uint8_t out, uint16_t out_mps);
void usbmsc_detach(void);
blockdev_t *usbmsc_device(void);
