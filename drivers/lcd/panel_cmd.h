#pragma once
#include <m5os/types.h>

/* One panel initialisation command: DCS command byte, parameters, parameter count, settle time afterwards. */
typedef struct { uint8_t cmd; const uint8_t *data; uint8_t len; uint16_t delay_ms; } panel_cmd_t;
