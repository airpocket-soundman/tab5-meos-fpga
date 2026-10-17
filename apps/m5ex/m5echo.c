/* The same echo server as a LOADED app: messages cross between kernel tasks and loaded code. */
#include "../sdk/m5ex_sdk.h"

M5EX_APP("m5echo", M5_PERM_IPC, 4096);

void app_main(const m5_api_t *api, void *arg)
{
    (void)arg;
    for (;;) {
        m5_msg_t m;
        if (api->msg_receive(&m, M5_WAIT_FOREVER) != 0) return;
        m.data[sizeof m.data - 1] = 0;
        if (m.data[0] == 'q' && m.data[1] == 'u' && m.data[2] == 'i' && m.data[3] == 't' && m.data[4] == 0) return;

        m5_msg_t r;
        r.type = m.type | 0x8000u;
        api->snprintf((char *)r.data, sizeof r.data, "m5echo:%s", (const char *)m.data);
        api->msg_send(m.sender, &r, 200);
    }
}
