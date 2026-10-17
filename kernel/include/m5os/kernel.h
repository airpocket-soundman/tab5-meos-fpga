#pragma once
#include <m5os/types.h>

#define M5OS_VERSION_STRING  "0.1"

void kernel_main(void) __attribute__((noreturn));
void kernel_panic(const char *msg) __attribute__((noreturn));
