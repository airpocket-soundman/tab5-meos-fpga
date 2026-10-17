#include "../sdk/m5ex_sdk.h"

M5EX_APP("m5hello", M5_PERM_CONSOLE, 4096);

void app_main(const m5_api_t *api, void *arg)
{
    (void)arg;
    int out = api->open("/dev/uart", M5_O_WRITE);
    if (out < 0) return;
    char line[96];
    int n = api->snprintf(line, sizeof line, "Hello from a LOADED M5EX app (pid %u, loaded code, API v%u)\n",
                          api->task_id(), api->version);
    api->write(out, line, (size_t)n);
    api->close(out);
}
