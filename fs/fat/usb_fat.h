#pragma once
#include <m5os/blockdev.h>
bool usb_fat_mount(blockdev_t *bd);
void usb_fat_detach(void);

bool sd_fat_mount(blockdev_t *bd);

bool usb_fat_mount_slot(unsigned slot, blockdev_t *bd);
void usb_fat_detach_slot(unsigned slot);
bool usb_fat_slot_busy(unsigned slot);
