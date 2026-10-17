#!/bin/sh
# Rebuild the M5EX sample apps and regenerate the C table embedded in the firmware.
set -e
cd "$(dirname "$0")/.."
. tools/env.sh
python tools/m5exbuild.py --embed apps/m5ex/images.c apps/m5ex/m5hello.c apps/m5ex/m5test.c apps/m5ex/m5spin.c apps/m5ex/m5echo.c
