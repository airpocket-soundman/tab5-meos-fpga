#pragma once
#include <m5os/types.h>
/* Single SDIO HCI owner. RX/lost execute under the Wi-Fi lock: enqueue only,
 * never block, send packets, or call these ownership APIs from callbacks. */
typedef bool (*hosted_hci_receive_fn)(void *arg,uint8_t type,const uint8_t *packet,size_t length);
typedef void (*hosted_hci_lost_fn)(void *arg);
bool hosted_hci_available(void);
bool hosted_hci_claim(hosted_hci_receive_fn receive,hosted_hci_lost_fn lost,void *arg);
void hosted_hci_release(void *arg);
bool hosted_hci_send(uint8_t type,const uint8_t *packet,size_t length);

typedef struct { uint32_t tx_packets, rx_packets, rx_rejected, tx_failed; uint16_t last_opcode; uint8_t last_event, last_type; } hosted_hci_status_t;
void hosted_hci_status(hosted_hci_status_t *out);
