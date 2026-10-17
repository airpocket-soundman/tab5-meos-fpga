#include <m5os/wifi.h>
#include <m5os/kprintf.h>

const char *wifi_auth_name(uint8_t authmode)
{
    switch (authmode) {
    case WIFI_AUTH_OPEN:          return "OPEN";
    case WIFI_AUTH_WEP:           return "WEP";
    case WIFI_AUTH_WPA_PSK:       return "WPA";
    case WIFI_AUTH_WPA2_PSK:      return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:  return "WPA/2";
    case WIFI_AUTH_ENTERPRISE:    return "EAP";
    case WIFI_AUTH_WPA3_PSK:      return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
    case WIFI_AUTH_WAPI_PSK:      return "WAPI";
    case WIFI_AUTH_OWE:           return "OWE";
    default:                      return "WPA2";
    }
}

const char *wifi_state_name(wifi_state_t state)
{
    switch (state) {
    case WIFI_STATE_UNINIT:       return "UNINIT";
    case WIFI_STATE_DISCONNECTED: return "DISCONNECTED";
    case WIFI_STATE_SCANNING:     return "SCANNING";
    case WIFI_STATE_CONNECTING:   return "CONNECTING";
    case WIFI_STATE_CONNECTED:    return "CONNECTED";
    case WIFI_STATE_ERROR:        return "ERROR";
    default:                      return "UNKNOWN";
    }
}

bool wifi_init(void) { return false; }
void wifi_boot_start(void) { }
int  wifi_scan(wifi_ap_t *out_aps, int max_aps) { (void)out_aps; (void)max_aps; return -1; }
bool wifi_connect(const char *ssid, const char *password, bool save) { (void)ssid; (void)password; (void)save; return false; }
bool wifi_disconnect(void) { return false; }
bool wifi_load_saved(char *ssid_out, char *pass_out, bool *auto_conn_out) { (void)ssid_out; (void)pass_out; (void)auto_conn_out; return false; }
bool wifi_save_config(const char *ssid, const char *password, bool auto_connect) { (void)ssid; (void)password; (void)auto_connect; return false; }
bool wifi_set_auto_connect(bool enable) { (void)enable; return false; }
bool wifi_forget(void) { return false; }

void wifi_get_status(wifi_status_t *out)
{
    if (!out) return;
    for (size_t i = 0; i < sizeof(*out); i++) ((uint8_t *)out)[i] = 0;
    out->state = WIFI_STATE_UNINIT;
    ksnprintf(out->last_msg, sizeof out->last_msg, "Wi-Fi driver not supported on this board");
}
