# FatFs for the MeOS USB adapter

Sources: ESP-IDF v5.4.4, components/fatfs/src (ChaN FatFs R0.15 w/patch2).
Original copyright and redistribution notice are retained in ff.c/ffunicode.c.

Local ffconf.h changes: one volume, 512-byte sectors, FAT12/16/32, UTF-8 long filenames on the task stack, 16 open-object locks, no formatter, fixed 2022-01-01 timestamps, no dynamic buffers, no FreeRTOS includes or internal mutex hooks. The MeOS VFS adapter serializes all FatFs operations with a MeOS mutex. No exFAT or USB hub support is provided by this integration.

The adapter is mounted at /usb only after the USB BOT/SCSI capacity probe succeeds. It never formats a device. VFS currently limits directory entry names to 23 bytes; long names are listed using their available 8.3 alias. Full paths remain subject to VFS_PATH_MAX.
