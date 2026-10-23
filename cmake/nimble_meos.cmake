# Optional Apache NimBLE host; include after idf_component_register(), then call
# m5os_add_nimble(${COMPONENT_LIB}). Disabled unless explicitly selected.
option(M5OS_BLUETOOTH "Enable experimental MeOS NimBLE BLE HID host" OFF)
get_filename_component(M5OS_NIMBLE_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
function(m5os_add_nimble target)
    if(NOT M5OS_BLUETOOTH)
        return()
    endif()
    if(NOT "$ENV{M5OS_BOARD}" STREQUAL "tab5")
        message(FATAL_ERROR "The Hosted NimBLE transport requires Tab5")
    endif()
    set(root "${M5OS_NIMBLE_REPO_ROOT}")
    set(n "${root}/third_party/nimble")
    set(groups porting/nimble/src nimble/src nimble/host/src nimble/host/util/src
        nimble/host/services/gap/src nimble/host/services/gatt/src nimble/host/store/ram/src
        nimble/transport/src ext/tinycrypt/src)
    set(sources)
    foreach(group IN LISTS groups)
        file(GLOB selected CONFIGURE_DEPENDS "${n}/${group}/*.c")
        list(APPEND sources ${selected})
    endforeach()
    # Upstream CPU/controller timers are not used by an external-C6 host.
    list(FILTER sources EXCLUDE REGEX "/(hal_timer|os_cputime|os_cputime_pwr2|ble_gatts_lcl)\\.c$")
    set(includes "${root}/drivers/bluetooth/npl" "${n}/nimble/include"
        "${n}/nimble/host/include" "${n}/nimble/host/util/include"
        "${n}/nimble/host/services/gap/include" "${n}/nimble/host/services/gatt/include"
        "${n}/nimble/host/store/ram/include" "${n}/nimble/transport/include"
        "${n}/porting/nimble/include" "${n}/ext/tinycrypt/include")
    target_include_directories(${target} PRIVATE ${includes})
    target_compile_definitions(${target} PRIVATE M5OS_BLUETOOTH=1 NIMBLE_CFG_CONTROLLER=0
        MYNEWT_VAL_BLE_ROLE_PERIPHERAL=0 MYNEWT_VAL_BLE_ROLE_BROADCASTER=0
        MYNEWT_VAL_BLE_SM_SC=1 MYNEWT_VAL_BLE_SM_SC_ONLY=1 MYNEWT_VAL_BLE_SM_LEGACY=0
        MYNEWT_VAL_BLE_MAX_CONNECTIONS=1 MYNEWT_VAL_BLE_TRANSPORT_HS__native=1
        MYNEWT_VAL_BLE_TRANSPORT_ACL_FROM_HS_COUNT=4 MYNEWT_VAL_BLE_TRANSPORT_ACL_FROM_LL_COUNT=4
        MYNEWT_VAL_BLE_TRANSPORT_EVT_DISCARDABLE_COUNT=4
        MYNEWT_VAL_BLE_TRANSPORT_LL__native=0 MYNEWT_VAL_BLE_TRANSPORT_LL__custom=1
        MYNEWT_VAL_BLE_TRANSPORT_LL__socket=0 MYNEWT_VAL_BLE_TRANSPORT_EVT_SIZE=257)
    # Upstream allocation exclusively uses the MeOS heap; preserve upstream files.
    set_source_files_properties(${sources} PROPERTIES
        COMPILE_DEFINITIONS "malloc=meos_bt_malloc;calloc=meos_bt_calloc;realloc=meos_bt_realloc;free=meos_bt_free"
        COMPILE_OPTIONS "-Wno-error")
    set_property(SOURCE "${n}/nimble/host/src/ble_uuid.c" APPEND PROPERTY
        COMPILE_DEFINITIONS "sprintf=meos_bt_sprintf")
    set(npl "${CMAKE_CURRENT_BINARY_DIR}/meos_nimble_npl.c")
    set(app "${CMAKE_CURRENT_BINARY_DIR}/meos_nimble_host.c")
    file(WRITE "${npl}" "#include \"${root}/drivers/bluetooth/npl/meos_npl.inc\"\n")
    file(WRITE "${app}" "#include <m5os/arch.h>\n#include <m5os/task.h>\n#include \"${root}/drivers/bluetooth/npl/hosted_nimble.inc\"\n#include \"${root}/drivers/bluetooth/npl/nimble_app.inc\"\n#include \"${root}/drivers/bluetooth/npl/meos_allocator.inc\"\n#include \"${root}/drivers/bluetooth/npl/meos_text.inc\"\n")
    target_sources(${target} PRIVATE ${sources} "${npl}" "${app}")
endfunction()
