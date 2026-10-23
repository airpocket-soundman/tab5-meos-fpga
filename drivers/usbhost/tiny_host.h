#pragma once
#include <m5os/types.h>
void meos_usb_lock(void);
void meos_usb_unlock(void);
void meos_usb_poll_locked(void);
bool meos_usb_init(void);
