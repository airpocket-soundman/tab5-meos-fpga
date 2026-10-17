#pragma once
#include <m5os/types.h>

/* Executing code that was copied into RAM at run time. */

/* The address through which [p, p+len) can be EXECUTED, or 0 if that RAM cannot
 * be fetched from.  ESP32-S3: internal SRAM shared between the data bus and the
 * instruction bus appears at two addresses (see SOC_I_D_OFFSET); the same bytes
 * are written/read through one and executed through the other. */
uintptr_t hal_exec_alias(const void *p, size_t len);

/* Make code just written through the data bus visible to instruction fetch. */
void hal_exec_sync(void *p, size_t len);
