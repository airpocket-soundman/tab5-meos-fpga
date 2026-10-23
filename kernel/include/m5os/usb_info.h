#pragma once
#include <m5os/types.h>

#define USB_INFO_MAX 16
#define USB_INFO_HUB 1
#define USB_INFO_KEYBOARD 2
#define USB_INFO_STORAGE 4
#define USB_INFO_ETHERNET 8
typedef struct {
    uint16_t vid, pid;
    uint8_t address, parent, port, speed, device_class, kinds;
    int8_t storage_slot;
    bool present, configured, mounted;
} usb_info_t;
/* Copies cached enumeration state; never probes devices or takes the USB bus. */
size_t usb_native_snapshot(usb_info_t *out, size_t capacity);
