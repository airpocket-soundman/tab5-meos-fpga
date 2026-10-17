/* Echo server: replies to every message with "echo:<text>".  Send it "quit" to make it exit. */
#include "../apps.h"
#include <m5os/msg.h>
#include <m5os/vfs.h>
#include <m5os/kprintf.h>
#include <m5os/task.h>

#define ECHO_REPLY_FLAG  0x8000u
#define ECHO_TIMEOUT_MS  200u

static bool is_quit(const char *s) { return s[0] == 'q' && s[1] == 'u' && s[2] == 'i' && s[3] == 't' && s[4] == 0; }

void echod_main(void *arg)
{
    (void)arg;
    int out = vfs_open("/dev/uart", VFS_O_WRITE);
    for (;;) {
        msg_t m;
        if (msg_receive(&m) != 0) break;
        m.data[MSG_DATA_SIZE - 1] = 0;                           /* never trust the sender to terminate it */
        const char *text = (const char *)m.data;
        if (out >= 0) {
            char line[64];
            int n = ksnprintf(line, sizeof line, "echod: from pid %u: %s\n", m.sender, text);
            vfs_write(out, line, (size_t)n);
        }
        if (is_quit(text)) break;
        msg_t r = { .type = m.type | ECHO_REPLY_FLAG };
        ksnprintf((char *)r.data, sizeof r.data, "echo:%s", text);
        msg_send_timeout(m.sender, &r, ECHO_TIMEOUT_MS);
    }
    if (out >= 0) vfs_close(out);
}
