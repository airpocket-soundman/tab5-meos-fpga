# TinyUSB host integration

Source: hathach/tinyusb tag 0.19.0, commit 331c263400dd5ccf77264cab1fa6bbd9f374b44e. MIT license retained. This directory contains the selected common, host, HID, MSC and Synopsys DWC2 source trees; CMake explicitly compiles only host files.

MeOS uses OPT_OS_NONE. A MeOS mutex serializes host processing and block I/O. The MeOS USB task polls hcd_int_handler(1, false) and tuh_task_ext(0, false). TinyUSB port 1 is ESP32-P4's high-speed controller; this numbering differs from ESP-IDF HAL port 0 for that controller. PHY initialization remains ESP-IDF usb_new_phy with the UTMI PHY.

Local change: dwc2_esp32.h removes FreeRTOS includes and CPU interrupt allocation, replaces dwc2_int_set with a no-op (all callers are serialized and processing is polled), and replaces the unused device remote-wakeup FreeRTOS delay with a ROM delay. The configuration selects FIFO/slave mode, not DMA. No TinyUSB class or DWC2 transfer logic is modified.

The first integration enables one MSC LUN 0 and boot HID keyboards. Hub support remains disabled until the next implementation/verification stage. USB and SD FAT volumes remain independent. The asynchronous MSC transfer uses a persistent bounce buffer so timeout handling cannot leave a controller writing to freed caller memory. After an MSC error, further block I/O is refused until reconnect.
