cmake_minimum_required(VERSION 3.16)

# Board selection: M5OS_BOARD=tab5, or the default cores3.  Use tools/build.sh.
if("$ENV{M5OS_BOARD}" STREQUAL "tab5")
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.tab5")
else()
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.cores3")
endif()

# Native M5EX apps are trusted machine code, not sandboxed programs. The P4
# SDK's locked RX/RW split deliberately forbids executing ordinary RAM heaps.
# Opt in explicitly for a dedicated build; all other PMP/PMA guards remain.
option(M5OS_NATIVE_M5EX "Allow trusted native M5EX on P4 using SDK RWX IDRAM policy" OFF)
if(M5OS_NATIVE_M5EX)
    if(NOT "$ENV{M5OS_BOARD}" STREQUAL "tab5")
        message(FATAL_ERROR "M5OS_NATIVE_M5EX currently targets Tab5/ESP32-P4 only")
    endif()
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.defaults.native_m5ex")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(m5os)
