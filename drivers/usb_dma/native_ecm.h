#pragma once
#include <m5os/types.h>
/* probe is called with the native USB bus locked; attach/service own the lock. */
bool native_ecm_probe(uint8_t address, uint16_t ep0_mps, uint8_t configurations, int *config);
bool native_ecm_attach(void);
void native_ecm_service(void);
void native_ecm_detach(void);
