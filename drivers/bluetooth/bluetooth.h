#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Single-owner, polled Hosted HCI probe. No RTOS, heap, pairing or connections.
 * Serialize all calls with the SDIO owner. send() must copy synchronously and
 * must NOT recursively call bt_hci_receive()/bt_hci_poll(). Packet type goes in
 * esp_payload_header.hci_pkt_type, NOT at the start of its payload. */
typedef bool (*bt_hci_send_fn)(void *arg, uint8_t type, const uint8_t *packet, size_t length);
typedef struct {
    uint8_t address_type, address[6], event_type;
    int8_t rssi;
    bool hid_service;
    char name[32]; /* Sanitized UTF-8 bytes for diagnostics; no control bytes. */
} bt_advertisement_t;
typedef void (*bt_hci_advertisement_fn)(void *arg, const bt_advertisement_t *device);
typedef enum { BT_HCI_OFF, BT_HCI_INITIALIZING, BT_HCI_READY, BT_HCI_SCANNING, BT_HCI_FAILED } bt_hci_state_t;
typedef enum { BT_HCI_ERROR_NONE, BT_HCI_ERROR_TRANSPORT, BT_HCI_ERROR_TIMEOUT,
               BT_HCI_ERROR_CONTROLLER, BT_HCI_ERROR_PROTOCOL } bt_hci_error_t;
typedef struct {
    bt_hci_state_t state;
    bt_hci_error_t error;
    uint16_t opcode, manufacturer, revision;
    uint8_t controller_status, hci_version, address[6];
    uint32_t received, malformed, advertisements;
} bt_hci_status_t;
typedef struct {
    bt_hci_send_fn send;
    bt_hci_advertisement_fn advertisement;
    void *arg;
    bt_hci_status_t status;
    uint64_t deadline, scan_deadline;
    uint8_t phase, credits;
    bool waiting, stop_requested;
} bt_hci_t;
/* Caller has verified SDIO transport is ready. Explicitly starts controller
 * reset; do not call on an HCI controller already owned by another host stack. */
bool bt_hci_open(bt_hci_t *h, bt_hci_send_fn send, bt_hci_advertisement_fn adv, void *arg, uint64_t now_ms);
void bt_hci_poll(bt_hci_t *h, uint64_t now_ms);
/* Return false for malformed/unsupported packets. Event body excludes H4 type.
 * Receive never transmits and can run from the WiFi RX dispatch under its lock. */
bool bt_hci_receive(bt_hci_t *h, uint8_t type, const uint8_t *packet, size_t length);
bool bt_hci_scan(bt_hci_t *h, uint32_t duration_ms, uint64_t now_ms);
void bt_hci_scan_stop(bt_hci_t *h);
/* After a C6 reset/SDIO teardown: clear local state; no implicit HCI reset. */
void bt_hci_transport_lost(bt_hci_t *h);

/* Boot-protocol HID decoder, reusable by an eventual authenticated HOGP client.
 * Report callbacks are impossible until its owner marks encryption/authentication
 * established. This is NOT pairing, GATT discovery or a Bluetooth host stack. */
typedef void (*bt_hid_key_fn)(void *arg, uint8_t usage, bool pressed);
typedef void (*bt_hid_mouse_fn)(void *arg, uint8_t buttons, int16_t dx, int16_t dy, int16_t wheel);
typedef struct {
    bool authenticated;
    uint8_t modifiers, keys[6];
    bt_hid_key_fn key;
    bt_hid_mouse_fn mouse;
    void *arg;
} bt_hid_boot_t;
void bt_hid_boot_init(bt_hid_boot_t *h, bt_hid_key_fn key, bt_hid_mouse_fn mouse, void *arg);
void bt_hid_boot_set_authenticated(bt_hid_boot_t *h, bool authenticated);
bool bt_hid_boot_keyboard(bt_hid_boot_t *h, const uint8_t *report, size_t length);
bool bt_hid_boot_mouse(bt_hid_boot_t *h, const uint8_t *report, size_t length);

/* HOGP callback adapter for a future BLE host stack. It issues no ACL/SMP/GATT
 * packets itself. Host-stack callbacks must identify the selected connection;
 * connection security is never inferred from receipt of an input notification. */
typedef struct {
    uint16_t service_start,service_end,protocol_mode;
    uint16_t keyboard_value,keyboard_ccc,mouse_value,mouse_ccc;
} bt_hogp_handles_t;
typedef enum { BT_HOGP_NO_ACTION, BT_HOGP_WRITE_PROTOCOL, BT_HOGP_SUBSCRIBE_KEYBOARD,
               BT_HOGP_SUBSCRIBE_MOUSE } bt_hogp_action_t;
typedef struct {
    bt_hid_boot_t input;
    bt_hogp_handles_t handles;
    uint16_t connection;
    bool connected,secure,protocol_set,keyboard_subscribed,mouse_subscribed;
    bool write_pending;
    uint32_t write_token;
    bt_hogp_action_t action;
} bt_hogp_t;
void bt_hogp_init(bt_hogp_t *h,bt_hid_key_fn key,bt_hid_mouse_fn mouse,void *arg);
/* Invoked only after user selects a peer and the host reports its connection. */
bool bt_hogp_connect(bt_hogp_t *h,uint16_t connection);
bool bt_hogp_discovered(bt_hogp_t *h,uint16_t connection,const bt_hogp_handles_t *handles);
/* pairing_accepted is the host's completed pairing/bond policy decision. A Just
 * Works device needs explicit user consent at that layer; discovery is not it.
 * Encryption requires a full 128-bit key. Failed security revokes held input. */
void bt_hogp_security(bt_hogp_t *h,uint16_t connection,bool encrypted,bool pairing_accepted,uint8_t key_bytes);
/* Returns a serialized GATT write action. Protocol Mode=0 uses Write Without
 * Response: complete after the host accepts that write. CCC=01 00 uses an ATT
 * Write Request: complete only after its remote Write Response. Preserve token. */
bt_hogp_action_t bt_hogp_next(bt_hogp_t *h,uint16_t *attribute,uint8_t value[2],size_t *length,uint32_t *token);
void bt_hogp_write_complete(bt_hogp_t *h,uint16_t connection,uint32_t token,bool success);
bool bt_hogp_notify(bt_hogp_t *h,uint16_t connection,uint16_t attribute,const uint8_t *report,size_t length);
void bt_hogp_disconnect(bt_hogp_t *h,uint16_t connection);
