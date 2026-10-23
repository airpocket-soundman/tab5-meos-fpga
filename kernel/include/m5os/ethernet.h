#pragma once
#include <m5os/types.h>
typedef struct {
    bool detected, ready, link, active;
    uint8_t mac[6], ip[4], netmask[4], gateway[4];
    uint32_t speed_mbps, rx_frames, tx_frames, dropped, usb_errors;
    uint32_t max_service_gap_us, max_bus_wait_us, max_network_poll_us;
} ethernet_status_t;
void usb_ethernet_status(ethernet_status_t *out);
