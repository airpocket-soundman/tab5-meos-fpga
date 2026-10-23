/* PHY power and clock setup stays in IDF. TinyUSB owns the DWC2 core. */
#include "esp_private/usb_phy.h"
#include <m5os/types.h>
static usb_phy_handle_t phy;
bool meos_usb_phy_init(void) {
    if (phy)
        return true;
    usb_phy_config_t cfg = {.controller = USB_PHY_CTRL_OTG,
                            .target = USB_PHY_TARGET_UTMI,
                            .otg_mode = USB_OTG_MODE_HOST,
                            .otg_speed = USB_PHY_SPEED_UNDEFINED};
    return usb_new_phy(&cfg, &phy) == ESP_OK;
}
