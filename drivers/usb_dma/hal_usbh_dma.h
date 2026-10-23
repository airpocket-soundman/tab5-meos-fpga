#pragma once
#include <m5os/types.h>

/*
 * USB host boundary: one root port, one control pipe, polled (no interrupts).
 *
 * This is deliberately smaller than a USB stack.  It gives a driver exactly what it needs to enumerate
 * a device and talk to it with control transfers; the HID driver (drivers/usbkbd) builds on it.
 * Waiting - debounce, reset hold, recovery - is the DRIVER's job (it can sleep); the calls here never
 * wait longer than one transfer.
 */
typedef enum { USBH_EVENT_NONE = 0, USBH_EVENT_CONNECT, USBH_EVENT_DISCONNECT, USBH_EVENT_ENABLED, USBH_EVENT_DISABLED, USBH_EVENT_OVERCURRENT } usbh_event_t;
typedef enum { USBH_SPEED_LOW = 0, USBH_SPEED_FULL, USBH_SPEED_HIGH } usbh_speed_t;

bool         hal_usbh_init(void);                    /* power the PHY and the core, power the port; false on failure */
usbh_event_t hal_usbh_poll(void);                    /* the next port event, or USBH_EVENT_NONE */
void         hal_usbh_debounce_done(void);           /* after the connect debounce delay: allow connect/disconnect events again */
bool         hal_usbh_connected(void);
void         hal_usbh_reset(bool assert_reset);      /* drive / release bus reset (hold it >= 10 ms) */
usbh_speed_t hal_usbh_speed(void);                   /* valid once the port is enabled (after a reset) */
bool         hal_usbh_open_control(uint8_t dev_addr, uint16_t max_packet);   /* (re)configure the control pipe; false if no pipe */
void hal_usbh_prepare_device(uint8_t address, bool low_speed_via_full_speed_hub);
/* Route a device through the translator in a high-speed hub, or hub=port=0 at root. */
void hal_usbh_set_route(uint8_t address, usbh_speed_t speed, uint8_t hub, uint8_t port);

#define USBH_ERR_TIMEOUT   (-1)
#define USBH_ERR_STALL     (-2)
#define USBH_ERR_XFER      (-3)
#define USBH_ERR_SIZE      (-4)
/* One control transfer.  setup is the 8-byte SETUP packet; data is the data stage (may be NULL if
 * setup's wLength is 0).  Returns the number of data bytes moved, or a negative USBH_ERR_*. */
int          hal_usbh_control(const uint8_t setup[8], void *data, size_t len);

void         hal_usbh_dump(void);                    /* bring-up aid: print the port/core status registers */

/* One additional, asynchronous data pipe. Owner serializes control/data operations.
 * poll returns USBH_PENDING while an idle interrupt endpoint NAKs normally. */
#define USBH_PENDING (-5)
bool hal_usbh_data_open(uint8_t address, uint8_t endpoint, uint16_t mps,
                        bool interrupt, uint8_t interval);
void hal_usbh_data_close(void);
int hal_usbh_data_start(void *buf, size_t len);
int hal_usbh_data_poll(void);

/* One separate boot-keyboard interrupt pipe. Bus owner serializes API calls;
 * its pending DMA transfer may overlap the bulk pipe. */
bool hal_usbh_keyboard_open(uint8_t address, uint8_t endpoint, uint16_t mps, uint8_t interval);
void hal_usbh_keyboard_close(void);
bool hal_usbh_keyboard_start(void);
int hal_usbh_keyboard_poll(uint8_t report[8]);

/* Dedicated ECM RX, TX and notification channels, separate from storage/HID. */
bool hal_usbh_net_open(unsigned pipe,uint8_t address,uint8_t endpoint,uint16_t mps,
                       bool interrupt,uint8_t interval);
void hal_usbh_net_close(unsigned pipe);
bool hal_usbh_net_start(unsigned pipe,void *buffer,size_t length);
int hal_usbh_net_poll(unsigned pipe);
