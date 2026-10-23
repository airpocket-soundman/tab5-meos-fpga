# Tab5 microSD implementation

The initial Tab5 implementation reuses the MeOS SPI SD block driver with GPIO CS42, CLK43, MOSI44 and MISO39. SPI is polled and bit-banged; it does not use the IDF FreeRTOS SD driver. Initialization uses a slow clock; normal transfers use the GPIO loop. Throughput has not been measured or optimized.

FatFs has two independent volumes: USB at /usb and microSD at /sd. Every open file records its owning volume. All FatFs calls share a MeOS mutex. The RAM root remains at /. No card is automatically formatted. FAT12/16/32 are supported; exFAT is not enabled. This is not SD hot-plug support: insert the card before boot, and keep it inserted during use.

## Hardware verification, 2026-10-23

- Inserted card recognized as SDHC/SDXC, 31,090,688 sectors of 512 bytes (15,181 MiB).
- Mounted existing FAT at /sd and listed its root.
- Confirmed a new test path did not exist before creating it.
- Wrote and read back a 38-byte text file with the shell put/cat commands. Transfer returned CRC32 3c7b6c41.
- Reset the device and read the same content again.
- Verified /sd and /usb mounted at the same time.
- Host FAT integration test passes independent volume access, and SD access after USB volume unmount, in addition to the existing FAT tests.

Not verified: sustained writes, large files, power interruption, media removal, filesystem damage handling, performance and every SD card type. The small test file remains on the card. USB write timeout diagnosis is a separate task.
