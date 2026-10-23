# Tiny native SL port. Original third_party/sl remains unchanged and credited.
get_filename_component(M5OS_SL_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
function(m5os_add_sl target)
    option(M5OS_SL "Build the native SL animation with a minimal ANSI adapter" ON)
    if(M5OS_SL)
        target_sources(${target} PRIVATE "${M5OS_SL_ROOT}/user/sl/sl_app.c")
        target_compile_definitions(${target} PRIVATE M5OS_SL=1)
    endif()
endfunction()
