# MeOS MicroPython v1.26.1 integration. Include after idf_component_register().
# Upstream py.cmake supplies the exact core source list; its POSIX-shell-dependent
# mkrules.cmake is replaced by a Python orchestration of the same generators.
set(_m5os_mp_default OFF)
if("$ENV{M5OS_BOARD}" STREQUAL "tab5")
    set(_m5os_mp_default ON)
endif()
option(M5OS_MICROPYTHON "Build the native MeOS MicroPython interpreter" ${_m5os_mp_default})
get_filename_component(M5OS_MP_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

function(m5os_add_micropython target)
    if(NOT M5OS_MICROPYTHON)
        return()
    endif()
    if(NOT "$ENV{M5OS_BOARD}" STREQUAL "tab5")
        message(FATAL_ERROR "The initial MeOS MicroPython port requires Tab5")
    endif()
    set(root "${M5OS_MP_REPO_ROOT}")
    set(MICROPY_DIR "${root}/third_party/micropython")
    set(port "${root}/user/micropython")
    if(NOT EXISTS "${MICROPY_DIR}/py/py.cmake" OR NOT EXISTS "${port}/mpconfigport.h")
        message(FATAL_ERROR "Pinned MicroPython sources or MeOS port are missing; no build-time download is performed")
    endif()
    include("${MICROPY_DIR}/py/py.cmake")
    file(GLOB port_sources "${port}/*.c")
    set(sources ${MICROPY_SOURCE_PY} ${port_sources}
        "${MICROPY_DIR}/shared/readline/readline.c"
        "${MICROPY_DIR}/shared/runtime/pyexec.c")
    set(generated "${CMAKE_BINARY_DIR}/genhdr")
    set(settings "${CMAKE_CURRENT_BINARY_DIR}/micropython_cpp.settings")
    set(stamp "${generated}/micropython_meos.stamp")
    target_include_directories(${target} PRIVATE "${MICROPY_DIR}" "${port}" "${CMAKE_BINARY_DIR}")
    target_compile_definitions(${target} PRIVATE M5OS_MICROPYTHON=1)
    target_sources(${target} PRIVATE ${sources})
    # Upstream is preserved verbatim; upstream diagnostics are not project Werror.
    set_source_files_properties(${MICROPY_SOURCE_PY}
        "${MICROPY_DIR}/shared/readline/readline.c"
        "${MICROPY_DIR}/shared/runtime/pyexec.c"
        PROPERTIES COMPILE_OPTIONS "-w")
    # Upstream readline formats only an ANSI cursor-position %u here. Route
    # that single translation unit to the MeOS formatter, avoiding newlib
    # stdio/FreeRTOS locks while leaving the vendored source unchanged.
    set_source_files_properties("${MICROPY_DIR}/shared/readline/readline.c"
        PROPERTIES COMPILE_DEFINITIONS "snprintf=ksnprintf")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    # Generated at CMake generation time, after the target's final includes and
    # defines are known. Prefix records avoid shell quoting and command limits.
    file(GENERATE OUTPUT "${settings}" CONTENT
"compiler=${CMAKE_C_COMPILER}
flags=${CMAKE_C_FLAGS}
include=$<JOIN:$<TARGET_PROPERTY:${target},INCLUDE_DIRECTORIES>,\ninclude=>
define=$<JOIN:$<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS>,\ndefine=>
source=$<JOIN:${sources},\nsource=>
")
    file(GLOB_RECURSE generator_deps
        "${MICROPY_DIR}/py/*.py" "${MICROPY_DIR}/py/*.h"
        "${MICROPY_DIR}/shared/runtime/*.h" "${MICROPY_DIR}/shared/readline/*.h"
        "${port}/*.h")
    add_custom_command(OUTPUT "${stamp}"
        COMMAND "${Python3_EXECUTABLE}" "${root}/tools/gen_micropython.py"
            --settings "${settings}" --vendor "${MICROPY_DIR}" --port "${port}"
            --output "${generated}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${stamp}"
        DEPENDS "${settings}" "${root}/tools/gen_micropython.py" ${sources} ${generator_deps}
        BYPRODUCTS "${generated}/qstrdefs.generated.h" "${generated}/moduledefs.h"
            "${generated}/root_pointers.h" "${generated}/mpversion.h"
            "${generated}/compressed.data.h"
        VERBATIM)
    add_custom_target(m5os_micropython_headers DEPENDS "${stamp}")
    add_dependencies(${target} m5os_micropython_headers)
endfunction()
