#pragma once
#include <m5os/types.h>

void     hal_time_init(void);
uint64_t hal_time_now_us(void);       /* monotonic microseconds since boot */
