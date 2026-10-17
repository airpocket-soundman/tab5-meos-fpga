/* A new app, not part of the firmware: build it on the host, push it with tools/m5ex_push.py, run it. */
#include "../sdk/m5ex_sdk.h"

M5EX_APP("primes", M5_PERM_CONSOLE, 4096);

#define LIMIT 5000
static uint8_t composite[LIMIT];                    /* .bss: zeroed by the loader */

void app_main(const m5_api_t *api, void *arg)
{
    (void)arg;
    uint32_t t0 = api->uptime_ms();
    int count = 0, last = 0;
    for (int i = 2; i < LIMIT; i++) {
        if (composite[i]) continue;
        count++; last = i;
        for (int j = i * 2; j < LIMIT; j += i) composite[j] = 1;
    }
    char line[96];
    int n = api->snprintf(line, sizeof line, "primes below %d: %d (largest %d), computed in %u ms by loaded code\n",
                          LIMIT, count, last, api->uptime_ms() - t0);
    int out = api->open("/dev/uart", M5_O_WRITE);
    if (out >= 0) { api->write(out, line, (size_t)n); api->close(out); }
}
