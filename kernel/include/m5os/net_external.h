#pragma once
#include <m5os/wifi.h>
/* Queue-only callbacks: never take the USB bus or call back into the IP stack. */
typedef bool (*net_external_tx_t)(const uint8_t *frame, size_t length);
typedef int (*net_external_rx_t)(uint8_t *frame, size_t capacity);
void net_external_up(const uint8_t mac[6], net_external_tx_t tx, net_external_rx_t rx);
void net_external_down(void);
void net_external_poll(void);
bool net_external_status(wifi_status_t *out);
