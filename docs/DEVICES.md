# Device registry: storage and cameras by name

`kernel/include/m5os/devreg.h`, `kernel/dev/devreg.c`. Storage and cameras are named in one place, whatever they are
plugged into, the way a Linux system names them under `/dev`.

| name | class | what it is |
| --- | --- | --- |
| `sd0` | block | the Tab5 microSD card |
| `usb0` `usb1` `usb2` | block | USB mass storage in slot N (direct port, or behind a hub) |
| `ram0` | block | reserved for the RAM disk (not registered yet, the root file system still creates it itself) |
| `camera0` | camera | the built-in camera |
| `camera1` | camera | a USB (UVC) camera |

A driver adds its device when it appears (`devreg_add`) and removes it when it goes (`devreg_remove`); the names are
chosen by the driver and stay the same for the same slot. Consumers do not know the drivers:

- **FAT automounter** (`fs/fat/usb_fat.c`, `fat_automount_init()`): listens for block devices. `sd0` is mounted at
  `/sd`, `usbN` at `/usb`, `/usb1`, `/usb2`; removal unmounts. These are the calls the drivers used to make
  themselves, in the same task and the same order; the drivers now only register (`kernel/boot/kmain.c`,
  `drivers/usb_dma/native_keyboard.c`).
- **`/dev`** lists everything (`ls /dev`): block devices show their capacity as the size. They cannot be opened
  yet (`ERR_NOSYS`); raw access is not part of this step.
- **`lsdev`** prints the registry with the mount point of each block device.
- **Cameras**: `camsrc_resolve("camera1" | "/dev/camera1")` gives the camera source, and the MicroPython
  `camera.Camera("/dev/camera1")`, `camera.Camera("camera0")` and `camera.list()` use it. `"onboard"` and `"usb"`
  still work.

Listeners run in the caller's task outside the registry's critical section, so they may block (mounting does). On
removal they run before the entry disappears and before the driver frees the device. A new listener is told about
every device already registered, so the order of start-up does not matter. The table holds 16 devices and 4 listeners.

## Checked

- `python tests/devreg/run_tests.py`: add, find, duplicate and invalid names, remove, listener replay and removal
  order, mount strings, the table and listener limits.
- On the Tab5 (2026-10-27): `lsdev` shows `camera0`, `sd0` mounted at `/sd` through the automounter, `camera1`; `ls /sd`
  reads the card; `ls /dev` lists all of them; the camera is opened as `"/dev/camera1"` and `"camera0"` from MicroPython;
  after `usbreset` the USB camera registers again.
- **Not checked on hardware:** USB mass storage through the registry (`usb0`..`usb2`, with and without a hub), and
  removal of a mounted volume. The change routes the same calls through the registry, but nothing was plugged in.

## Not done

- Raw block access through `/dev/sd0` and frame access through `/dev/camera0`.
- The RAM disk (`ram0`) and the boards other than the Tab5 (the CoreS3 and Cardputer still mount their card directly).
- USB Ethernet, the keyboards and other classes: they are not registry devices yet.
