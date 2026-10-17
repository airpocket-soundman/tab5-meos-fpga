/* Sits in a loop holding memory: used to check that stopping a loaded app reclaims its code image too. */
#include "../sdk/m5ex_sdk.h"

M5EX_APP("m5spin", M5_PERM_CONSOLE, 4096);

void app_main(const m5_api_t *api, void *arg)
{
    (void)arg;
    volatile uint8_t *held = api->alloc(10000);
    if (held) held[0] = 1;
    for (;;) api->sleep_ms(100);
}
