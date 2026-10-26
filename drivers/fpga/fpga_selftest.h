#pragma once
#include <stdint.h>
/* Runs only the software endpoint. 513 valid exchanges plus injected failures. */
int fpga_mock_selftest(unsigned chunk,uint32_t *transactions,uint32_t *fault_checks);
