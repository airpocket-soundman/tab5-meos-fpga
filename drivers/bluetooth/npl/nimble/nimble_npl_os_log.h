#pragma once
/* Pairing/input buffers must not be dumped by a generic stack debug logger.
 * The MeOS adapter exposes structured errors through its own status API. */
#define BLE_NPL_LOG_IMPL(level) \
    static inline void _BLE_NPL_LOG_CAT(BLE_NPL_LOG_MODULE, _BLE_NPL_LOG_CAT(_, level))(const char *format, ...) { (void)format; }
