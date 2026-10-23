# M5OS lives in one IDF "component" only because IDF's build system wants
# one.  The real structure is the directory tree at the repository root.
set(M5OS_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

file(GLOB_RECURSE M5OS_SRCS
    ${M5OS_ROOT}/kernel/*.c   ${M5OS_ROOT}/kernel/*.S
    ${M5OS_ROOT}/drivers/*.c  ${M5OS_ROOT}/hal/*.c  ${M5OS_ROOT}/hal/*.S
    ${M5OS_ROOT}/fs/*.c       ${M5OS_ROOT}/user/*.c ${M5OS_ROOT}/apps/*.c
    ${M5OS_ROOT}/third_party/wasm3/*.c ${M5OS_ROOT}/third_party/quirc/*.c
    ${M5OS_ROOT}/third_party/fatfs/ff.c ${M5OS_ROOT}/third_party/fatfs/ffunicode.c)
# apps/m5ex/*.c and apps/sdk/*.c are SOURCES OF LOADABLE APPS, built separately by
# tools/m5exbuild.py with their own link layout; the firmware only embeds the generated images.c.
list(FILTER M5OS_SRCS EXCLUDE REGEX "/apps/sdk/|/apps/m5ex/[^i]|/apps/wasm/[^w]")
list(FILTER M5OS_SRCS EXCLUDE REGEX "/user/micropython/")
# keep images.c / images_none.c (generated / stub).  apps/wasm holds the sources of WebAssembly apps: only wasm_images.c is firmware.
set(M5OS_NO_WASM_RE "/drivers/usbmsc/|/drivers/usbhost/|/fs/fat/|/third_party/fatfs/|/third_party/wasm3/|/third_party/quirc/|/kernel/app/wasm_|/apps/wasm/wasm_images\\.c$|/user/wasmapps/")      # WebAssembly runs on the Tab5 only

set(M5OS_USB_BACKEND "native_dma" CACHE STRING "USB backend: native_dma or tinyusb")
set_property(CACHE M5OS_USB_BACKEND PROPERTY STRINGS native_dma tinyusb)
if(NOT M5OS_USB_BACKEND MATCHES "^(native_dma|tinyusb)$")
    message(FATAL_ERROR "Unknown USB backend: ${M5OS_USB_BACKEND}")
endif()
if("$ENV{M5OS_BOARD}" STREQUAL "tab5" AND M5OS_USB_BACKEND STREQUAL "tinyusb")
    list(APPEND M5OS_SRCS
        ${M5OS_ROOT}/third_party/tinyusb/src/tusb.c
        ${M5OS_ROOT}/third_party/tinyusb/src/common/tusb_fifo.c
        ${M5OS_ROOT}/third_party/tinyusb/src/host/usbh.c
        ${M5OS_ROOT}/third_party/tinyusb/src/host/hub.c
        ${M5OS_ROOT}/third_party/tinyusb/src/class/hid/hid_host.c
        ${M5OS_ROOT}/third_party/tinyusb/src/class/msc/msc_host.c
        ${M5OS_ROOT}/third_party/tinyusb/src/portable/synopsys/dwc2/hcd_dwc2.c
        ${M5OS_ROOT}/third_party/tinyusb/src/portable/synopsys/dwc2/dwc2_common.c)
endif()
if("$ENV{M5OS_BOARD}" STREQUAL "tab5" AND M5OS_USB_BACKEND STREQUAL "native_dma")
    list(FILTER M5OS_SRCS EXCLUDE REGEX "/drivers/usbkbd/usbkbd\\.c$|/drivers/usbmsc/|/drivers/usbhost/|/hal/esp32p4/usb_host\\.c$")
else()
    list(FILTER M5OS_SRCS EXCLUDE REGEX "/drivers/usb_dma/|/hal/esp32p4/usb_native_dma\\.c$")
endif()
# Board-specific sources: *_cores3.c / *_cardputer.c, the chip directories hal/esp32s3 (Xtensa) and
# hal/esp32p4 (RISC-V), and the drivers that only some boards have.
if("$ENV{M5OS_BOARD}" STREQUAL "cardputer")
    list(FILTER M5OS_SRCS EXCLUDE REGEX "_cores3\\.c$|/hal/esp32p4/|${M5OS_NO_WASM_RE}|/drivers/(touch|button|usbkbd|tab5kbd|ime)/|/drivers/lcd/(ili9342|display_tab5)\\.c$|/drivers/camera/camera_tab5\\.c$|/drivers/wifi/wifi_tab5\\.c$|/m5ex/images_none\\.c$")
    set(M5OS_BOARD_DEF M5OS_BOARD_CARDPUTER=1)
    set(M5OS_ARCH_REQUIRES xtensa)
elseif("$ENV{M5OS_BOARD}" STREQUAL "tab5")
    list(FILTER M5OS_SRCS EXCLUDE REGEX "_cores3\\.c$|_cardputer\\.c$|/hal/esp32s3/|/drivers/(touch|button|keyboard)/|/drivers/lcd/(ili9342|st7789|display)\\.c$|/drivers/camera/camera_stub\\.c$|/drivers/wifi/wifi_stub\\.c$|/m5ex/images\\.c$")
    set(M5OS_BOARD_DEF M5OS_BOARD_TAB5=1)
    set(M5OS_ARCH_REQUIRES riscv esp_psram esp_mm esp_lcd esp_driver_cam esp_driver_isp esp_driver_ledc usb)
else()
    list(FILTER M5OS_SRCS EXCLUDE REGEX "_cardputer\\.c$|/hal/esp32p4/|${M5OS_NO_WASM_RE}|/drivers/(keyboard|usbkbd|tab5kbd|ime)/|/drivers/lcd/(st7789|display_tab5)\\.c$|/drivers/camera/camera_tab5\\.c$|/drivers/wifi/wifi_tab5\\.c$|/m5ex/images_none\\.c$")
    set(M5OS_BOARD_DEF M5OS_BOARD_CORES3=1)
    set(M5OS_ARCH_REQUIRES xtensa)
endif()

# Developer switch: M5OS_SELFTEST=1 idf.py build   -> run the regression tests at boot instead of the shell
if("$ENV{M5OS_SELFTEST}" STREQUAL "1")
    set(M5OS_DEFS M5OS_BOOT_SELFTEST=1)
endif()

idf_component_register(
    SRCS ${M5OS_SRCS}
    INCLUDE_DIRS ${M5OS_ROOT}/drivers/usbhost ${M5OS_ROOT}/third_party/tinyusb/src ${M5OS_ROOT}/kernel/include ${M5OS_ROOT}/hal ${M5OS_ROOT}/third_party/wasm3 ${M5OS_ROOT}/third_party/quirc
    # Only low-level, FreeRTOS-free IDF pieces are linked in explicitly.
    PRIV_REQUIRES soc hal esp_rom esp_hw_support ${M5OS_ARCH_REQUIRES})

target_compile_definitions(${COMPONENT_LIB} PRIVATE ${M5OS_BOARD_DEF} ${M5OS_DEFS})

# Optional maintenance build only. Normal builds carry no C6 application image.
set(M5OS_C6_OTA_IMAGE "" CACHE FILEPATH "Pinned Tab5 C6 MCU application for explicit SDIO OTA")
if(M5OS_C6_OTA_IMAGE)
    if(NOT "$ENV{M5OS_BOARD}" STREQUAL "tab5")
        message(FATAL_ERROR "C6 maintenance image requires the Tab5 board")
    endif()
    if(NOT EXISTS "${M5OS_C6_OTA_IMAGE}")
        message(FATAL_ERROR "C6 MCU application does not exist: ${M5OS_C6_OTA_IMAGE}")
    endif()
    file(SHA256 "${M5OS_C6_OTA_IMAGE}" M5OS_C6_IMAGE_HASH)
    if(NOT M5OS_C6_IMAGE_HASH STREQUAL "cb169b09a890037af4250bd93c33c394037a55b801ba1cc0a4c1210e60c68902")
        message(FATAL_ERROR "C6 application differs from the verified official Tab5 image")
    endif()
    configure_file("${M5OS_C6_OTA_IMAGE}" "${CMAKE_CURRENT_BINARY_DIR}/c6_mcu_app.bin" COPYONLY)
    target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_BINARY_DIR}/c6_mcu_app.bin" BINARY)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE M5OS_C6_OTA_IMAGE=1)
endif()

if("$ENV{M5OS_BOARD}" STREQUAL "tab5" AND M5OS_USB_BACKEND STREQUAL "native_dma")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE M5OS_USB_NATIVE_DMA=1)
    set_source_files_properties(${M5OS_ROOT}/user/shell/shell.c PROPERTIES
        COMPILE_DEFINITIONS M5OS_USB_NATIVE_DMA=1)
endif()

# wasm3 (third_party/wasm3, MIT): the interpreter behind .wasm apps.  Vendored as is except for the allocator hook in
# m3_core.c; its own warnings are not ours to fix.  Optimised for speed: it is the hot loop of every WebAssembly app.
file(GLOB M5OS_WASM3_SRCS ${M5OS_ROOT}/third_party/wasm3/*.c)
set_source_files_properties(${M5OS_WASM3_SRCS} PROPERTIES COMPILE_OPTIONS "-w;-O3" COMPILE_DEFINITIONS "M5OS_WASM_ALLOC=1")

# THE hook that keeps FreeRTOS from ever running:
# startup.c calls esp_startup_start_app(); freertos/port/xtensa/port.c
# implements it as "create main task + vTaskStartScheduler()".  --wrap
# redirects that one call to __wrap_esp_startup_start_app() in
# kernel/boot/boot_hook.c.
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=esp_startup_start_app")

# quirc (third_party/quirc, ISC): QR-code reader for installing apps from the camera.  Vendored as is except for its allocator.
file(GLOB M5OS_QUIRC_SRCS ${M5OS_ROOT}/third_party/quirc/*.c)
set_source_files_properties(${M5OS_QUIRC_SRCS} PROPERTIES COMPILE_OPTIONS "-w;-O2")

include(${M5OS_ROOT}/cmake/nimble_meos.cmake)
m5os_add_nimble(${COMPONENT_LIB})
include(${M5OS_ROOT}/cmake/micropython_meos.cmake)
m5os_add_micropython(${COMPONENT_LIB})
