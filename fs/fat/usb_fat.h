#pragma once
#include <m5os/blockdev.h>
bool usb_fat_mount(blockdev_t *bd);
void usb_fat_detach(void);

bool sd_fat_mount(blockdev_t *bd);
