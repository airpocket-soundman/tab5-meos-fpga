# USB Ethernet on Tab5

The native DMA host recognizes the attached Realtek RTL8153 (`0bda:8153`)
behind a Realtek USB hub (`0bda:5411`). It selects USB configuration 2,
CDC-ECM, instead of implementing Realtek's proprietary configuration 1.
The current probe is deliberately limited to this device ID; support for other
USB Ethernet adapters has not been established.

## Data path

`RTL8153 CDC-ECM -> native DWC2 descriptor DMA -> bounded frame queues -> MeOS IPv4 stack`

The host uses separate RX, TX and notification DMA channels. These do not replace
the storage or keyboard channel. Descriptor and bounce buffers are allocated once
from internal DMA-capable memory with explicit alignment. Frame queues and CPU
buffers use PSRAM. Eight frames per direction bound memory use. Ethernet frames
are limited to 1514 bytes, with minimum-size TX padding and an explicit USB zero
length packet when a TX frame ends on a bulk max-packet boundary.

The driver reads the MAC string descriptor, selects the data alternate setting,
and enables directed and broadcast reception. Link status comes from CDC
notifications and a read-only RTL8153 PHY register. CDC speed notifications are
optional; a missing reported rate is not a measured speed of zero.

The existing MeOS IPv4 stack was separated from the Wi-Fi frame transport.
Ethernet takes the single active IP interface while its cable link is up.
Switching interfaces clears neighbor/socket state and starts DHCP on the selected
interface. This is not simultaneous routing across two interfaces. Physical Wi-Fi
status remains separately available through `wifi status`; applications use the
active-interface status. Wi-Fi fallback still requires a working C6 connection.

Network callbacks only enqueue/dequeue frames. They never take the USB bus lock.
USB servicing polls the network mutex without blocking behind DNS/ARP waits.
Complete storage BOT commands remain atomic; large filesystem requests release
the bus between commands so other USB classes can run.

## Diagnostics

`lsusb` reports the addressed Ethernet device and its hub port. `eth` reports
link, MAC, DHCP IPv4 address, packet/drop/error counters and maximum service,
bus-wait and network-poll latency. Counters are diagnostic snapshots.

Tab5 connects to this USB 3 hub using **USB 2 High-Speed (480 Mbit/s)**.
SuperSpeed operation and gigabit application throughput are not implied by the
adapter's product class. Actual throughput has not been benchmarked.

## Hardware results, 2026-10-23

- Hub, two USB drives, USB microSD reader and RTL8153 enumerate together.
- CDC-ECM configuration, link up and DHCP address assignment succeeded.
- PC-to-Tab5 ICMP echo passed for 32-byte payloads and for 470/982-byte payloads
  that exercise 512/1024-byte Ethernet frames and USB TX termination.
- Each USB drive passed a new 8192-byte write/read with matching CRC on this hub;
  one is exFAT and the other FAT. Further new 512-byte files also matched.
- Ethernet RX/TX continued after the storage tests, without USB transfer errors.
- Storage activity caused substantial packet loss and delay during a concurrent
  ping trial. Coexistence latency remains under investigation; successful file
  contents alone do not establish responsive concurrent networking.
- Inbound SSH authentication, console commands, disconnect/reconnect and stop
  passed on the device. Concurrent outbound SSH and interface switching are
  tracked separately in the SSH implementation notes.

Wi-Fi SDIO enumeration succeeds, but the device uses ESP-Hosted-NG while the
existing MeOS driver expects MCU. Wi-Fi IP communication has not passed. See
[firmware compatibility](WIFI_COMPATIBILITY.md).

## References

- [Linux RTL8153 ECM binding](https://github.com/torvalds/linux/blob/master/drivers/net/usb/r8153_ecm.c)
- [Realtek RTL8153 datasheet](https://www.olimex.com/Products/USB-Modules/Ethernet/USB-GIGABIT/resources/rtl8153.pdf)
- [ESP-IDF DWC2 HAL](https://github.com/espressif/esp-idf/blob/v5.4.4/components/hal/include/hal/usb_dwc_hal.h)

ECM descriptor/class-request handling is implemented independently; no Linux
network-driver source was copied into the firmware.
