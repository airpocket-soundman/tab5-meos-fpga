#pragma once
#include "bluetooth.h"
/* Optional NimBLE backend. Explicit peer selection; bonds are RAM-only. */
typedef enum { BT_HOST_OFF,BT_HOST_STARTING,BT_HOST_READY,BT_HOST_SCANNING,
    BT_HOST_CONNECTING,BT_HOST_PAIRING,BT_HOST_DISCOVERING,BT_HOST_INPUT,BT_HOST_ERROR } bt_host_state_t;
typedef struct {
    bt_host_state_t state;
    int error;
    uint16_t connection;
    bool encrypted,authenticated,bonded,justworks_allowed;
    uint8_t pairing_action;
    uint32_t comparison_number;
    uint32_t pairing_generation;
    unsigned devices,reports;
} bt_host_status_t;
bool bt_host_start(bt_hci_send_fn send,void *arg,bt_hid_key_fn key,bt_hid_mouse_fn mouse,void *input_arg);
/* Claim the generic Hosted SDIO HCI transport; re-enable reuses the host task. */
bool bt_host_enable(bt_hid_key_fn key,bt_hid_mouse_fn mouse,void *input_arg);
bool bt_host_scan(uint32_t duration_ms);
/* Address is in controller/HCI little-endian order, type 0 public or 1 random.
 * allow_justworks is explicit consent for pairing without MITM authentication. */
bool bt_host_connect(const uint8_t address[6],uint8_t type,bool allow_justworks);
bool bt_host_disconnect(void);
/* Untokened convenience accepts rejection only. Approval requires the exact
 * pairing_generation observed in status, and is revalidated at dispatch. */
bool bt_host_pair_reply(bool accept,uint32_t passkey);
bool bt_host_pair_reply_for(uint32_t generation,bool accept,uint32_t passkey);
bool bt_host_forget_bonds(void);
void bt_host_status(bt_host_status_t *out);
size_t bt_host_devices(bt_advertisement_t *out,size_t capacity);
bool bt_host_receive(uint8_t type,const uint8_t *packet,size_t length);
void bt_host_transport_lost(void);
