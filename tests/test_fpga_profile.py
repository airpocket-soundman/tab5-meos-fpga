"""Check optional FPGA integration using the real CMake module resolver.

The integration profile must preserve cameras/Python and add only the native
FPGA dependency pair. Existing published profiles must remain FPGA-free.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
script = '''
cmake_minimum_required(VERSION 3.16)
include("@ROOT@/cmake/modules.cmake")
set(M5OS_USB_BACKEND native_dma)
set(M5OS_PROFILE "${PROFILE}")
m5os_resolve_modules()
foreach(feature CAMERA UVC MJPEG CAMSRC MICROPYTHON MP_CAMERA SL)
  if(NOT M5OS_MODULE_${feature})
    message(FATAL_ERROR "${PROFILE} dropped ${feature}")
  endif()
endforeach()
if(PROFILE STREQUAL large_fpga)
  if(NOT M5OS_MODULE_FPGA OR NOT M5OS_MODULE_FPGA_PARLIO)
    message(FATAL_ERROR "native FPGA dependency missing")
  endif()
else()
  if(M5OS_MODULE_FPGA OR M5OS_MODULE_FPGA_PARLIO)
    message(FATAL_ERROR "published large profile changed")
  endif()
endif()
foreach(feature BLUETOOTH FPGA_GFX FPGA_NPU FPGA_DIAG GFXBENCH)
  if(M5OS_MODULE_${feature})
    message(FATAL_ERROR "unexpected optional module ${feature}")
  endif()
endforeach()
set(M5OS_SRCS /repo/drivers/fpga/fpga_parallel.c /repo/user/fpga/fpga_parallel_app.c
    /repo/drivers/fpga/fpga_wire.c /repo/drivers/fpga/fpga_tile_test.c
    /repo/drivers/fpga/fpga_surface.c /repo/drivers/fpga/fpga_stream.c /repo/hal/esp32p4/parlio_native.c
    /repo/drivers/camera/camera_tab5.c /repo/user/micropython/mp_hal.c)
@SOURCE_FILTER@
list(LENGTH M5OS_SRCS source_count)
if(PROFILE STREQUAL large_fpga)
  if(NOT source_count EQUAL 9)
    message(FATAL_ERROR "native sources unexpectedly excluded")
  endif()
else()
  if(NOT source_count EQUAL 2)
    message(FATAL_ERROR "optional native sources leaked into normal build")
  endif()
endif()
'''.replace('@ROOT@', root.as_posix())
source_filter = re.search(r'if\(NOT M5OS_MODULE_FPGA_PARLIO\).*?endif\(\)',
                          (root/'main/CMakeLists.txt').read_text(encoding='utf-8'), re.S)
assert source_filter, 'native source filter missing'
script = script.replace('@SOURCE_FILTER@', source_filter.group())
cmake = shutil.which('cmake')
if not cmake:
    raise SystemExit('cmake is required; no profile checks were run')
env = dict(os.environ, M5OS_BOARD='tab5')
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)/'profile.cmake'
    p.write_text(script, encoding='utf-8')
    for profile in ('large', 'large_fpga'):
        subprocess.run([cmake, '-DPROFILE='+profile, '-P', str(p)], env=env, check=True)
print('Native FPGA integration preserves large-profile camera/Python modules; published large remains unchanged.')
