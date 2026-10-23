# Tab5 C6 firmware compatibility

Upstream MeOS already contains a Tab5 SDIO and ESP-Hosted-MCU driver.
This fork keeps that implementation. A second, Linux-oriented NG host stack
is not required to validate the existing MCU driver with its expected firmware.

## Observed device, 2026-10-23

The C6 enumerated over SDIO. Its first frame was 40 bytes:

```text
03 00 00 00 1c 00 0c 00 00 00 00 00
```

This is an ESP-Hosted-NG INTERNAL startup header: interface 3, payload 28,
offset 12. The event metadata was code 1, status 0, body length 21 and data
length 20. TLV metadata was chip ID (tag 3, length 1), capability (tag 0,
length 1) and firmware data (tag 1, length 12). No credential payload was logged.

The MCU header expected by MeOS puts length at offset 2 and data offset at 4;
it cannot parse this NG event. NG uses a different control protocol rather than
MeOS's MCU Protobuf WifiInit request. Header offsets alone cannot bridge them.

The new recognition helper checks the complete startup structure and bounded
TLVs. On a match, Wi-Fi initialization stops with an explicit compatibility
message before sending MCU RPCs. Normal MCU traffic retains the original path.
The host test covers the observed length layout, truncated frames, invalid TLVs
and ordinary MCU headers. Wi-Fi IP traffic has not been verified on this C6.

## Matching firmware candidate

M5Stack publishes an ESP32-C6 MCU image for Tab5:

- File: `ESP32C6-WiFi-SDIO-Interface-V1.4.1-96bea3a_0x0.bin`
- Size: 1,144,432 bytes.
- SHA-256: `1173bf86ab86c8dbd3f9863d10b15c2e46f57000b2b77c803e2b79bd0a3fa38e`
- The published script writes the merged image to C6 flash offset `0x0`.
- [Official image](https://github.com/m5stack/M5Tab5-UserDemo/blob/main/platforms/tab5/wifi_c6_fw/ESP32C6-WiFi-SDIO-Interface-V1.4.1-96bea3a_0x0.bin)
- [Official C6 recovery instructions](https://docs.m5stack.com/en/guide/tab5/c6_wifi_restore)

The documented recovery route uses C6's dedicated connector and a USB-TTL
programmer such as the M5Stack ESP32 Downloader. A direct P4-USB-to-C6 UART
update path has not been established. This task has not written the C6.
Compatibility of this candidate with MeOS still needs a device test after an
authorized update; the firmware filename is not proof of working Wi-Fi.

NG contains OTA commands, but support and partition layout on the current device
have not been established. A merged `0x0` image is not an OTA application image.
No experimental SDIO firmware updater has been added.

## Primary references

- [MeOS Wi-Fi implementation](https://github.com/shi3z/MeOS/blob/main/drivers/wifi/wifi_tab5.c)
- [Espressif NG wire structures](https://github.com/espressif/esp-hosted/blob/legacy/fg-ng/esp_hosted_ng/host/include/adapter.h)
- [NG SDIO startup event construction](https://github.com/espressif/esp-hosted/blob/36c288f3/esp_hosted_ng/esp/esp_driver/network_adapter/main/sdio_slave_api.c)
