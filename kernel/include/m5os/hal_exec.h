#pragma once
#include <m5os/types.h>

/* Executing code that was copied into RAM at run time. */

/* The address through which [p, p+len) can be EXECUTED, or 0 if that RAM cannot
 * be fetched from.  ESP32-S3: internal SRAM shared between the data bus and the
 * instruction bus appears at two addresses (see SOC_I_D_OFFSET); the same bytes
 * are written/read through one and executed through the other. */
uintptr_t hal_exec_alias(const void *p, size_t len);

/* Make code just written through the data bus visible to instruction fetch.
 * P4 images must have isolated128-byte cache-line storage and128-byte alignment.
 * False means execution is forbidden or cache publication failed: do not launch. */
bool hal_exec_sync(void *p, size_t len);
