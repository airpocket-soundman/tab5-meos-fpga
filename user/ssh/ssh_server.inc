/* Included after ssh_app.c: reuse its SSH transport and crypto primitives.
 * One authenticated connection attaches to the existing console. This is not
 * a second shell or PTY: local keys and remote keys operate the same shell.
 */
#include <m5os/mutex.h>
#include "sftp_server.h"
#include "sftp_selftest.h"

#define SSHD_SOCK (WIFI_MAX_TCP_SOCKETS - 1)
#define SSHD_BANNER "SSH-2.0-MeOS_server_1.0"
#define SSHD_HOSTKEY_PATH "/home/ssh_host_ed25519"
#define SSHD_INPUT_WINDOW 4096u
#define SSHD_SESSION_LIMIT_US (3600ull * 1000000ull)

static volatile bool s_sshd_run, s_sshd_task_live, s_sshd_connected;
static uint16_t s_sshd_port = 22;
static bool s_sshd_storage_enabled=true,s_sshd_storage_writable=false;
static char s_sshd_user[33];
static uint8_t s_sshd_password_hash[32];
static uint8_t s_sshd_host_seed[32], s_sshd_host_pub[32];

static bool sshd_bytes_equal(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < n; ++i) diff |= a[i] ^ b[i];
    return diff == 0;
}

static bool sshd_string_eq(const uint8_t *s, size_t n, const char *value)
{
    return strlen(value) == n && !memcmp(s, value, n);
}

static bool sshd_name_in_list(const uint8_t *list, size_t n, const char *name)
{
    size_t at = 0;
    while (at < n) {
        size_t end = at;
        while (end < n && list[end] != ',') ++end;
        if (sshd_string_eq(list + at, end - at, name)) return true;
        at = end + 1;
    }
    return false;
}

static bool sshd_hostkey_load(void)
{
    int fd = vfs_open(SSHD_HOSTKEY_PATH, VFS_O_READ);
    if (fd >= 0) {
        uint8_t record[33];
        int n = vfs_read(fd, record, sizeof(record));
        vfs_close(fd);
        if (n != 32) {
            kputs("sshd: host key has invalid length; refusing to replace it\n");
            return false;
        }
        memcpy(s_sshd_host_seed, record, 32);
        memset(record, 0, sizeof(record));
    } else {
        if (fd != ERR_NOENT) return false;
        ssh_random_bytes(s_sshd_host_seed, sizeof(s_sshd_host_seed));
        fd = vfs_open(SSHD_HOSTKEY_PATH, VFS_O_WRITE | VFS_O_CREATE);
        if (fd < 0) return false;
        int n = vfs_write(fd, s_sshd_host_seed, sizeof(s_sshd_host_seed));
        int rc = vfs_close(fd);
        if (n != 32 || rc < 0) return false;
    }
    ed25519_seed_to_pub(s_sshd_host_seed, s_sshd_host_pub);
    return true;
}

/* Direction labels in ssh_session_t name the original client. For the server,
 * enc_c2s/mac_key_c2s are the outgoing server direction and dec_s2c the incoming
 * client direction; key derivation below deliberately swaps A/B, C/D and E/F. */
static bool sshd_handshake(ssh_session_t *s)
{
    static const char *names[10] = {
        "curve25519-sha256,curve25519-sha256@libssh.org", "ssh-ed25519", "aes128-ctr", "aes128-ctr",
        "hmac-sha2-256", "hmac-sha2-256", "none", "none", "", ""
    };
    const char banner[] = SSHD_BANNER "\r\n";
    if (wifi_tcp_send_sock(s->sock, banner, sizeof(banner) - 1, 8000) != sizeof(banner) - 1)
        return false;
    size_t vn = 0;
    for (;;) {
        uint8_t c;
        if (tcp_read_exact(s->sock, &c, 1, 10000, 10000) != 1) return false;
        if (c == '\n') break;
        if (c != '\r') {
            if (vn >= sizeof(s->v_s) - 1 || c < 32 || c > 126) return false;
            s->v_s[vn++] = c;
        }
    }
    s->v_s[vn] = 0;
    if (vn < 8 || memcmp(s->v_s, "SSH-2.0-", 8)) return false;

    /* i_c contains our (server) proposal; i_s contains the client's proposal. */
    size_t p = buf_put_u8(s->i_c, 0, SSH_MSG_KEXINIT);
    ssh_random_bytes(s->i_c + p, 16);
    p += 16;
    for (int i = 0; i < 10; i++) p = buf_put_str(s->i_c, p, names[i]);
    p = buf_put_u8(s->i_c, p, 0);
    s->i_c_len = buf_put_u32(s->i_c, p, 0);
    if (!ssh_send_packet(s, s->i_c, s->i_c_len)) return false;
    uint8_t *rx;
    int n = ssh_recv_packet(s, &rx, 15000);
    if (n < 17 || rx[0] != SSH_MSG_KEXINIT || n > sizeof(s->i_s)) return false;
    memcpy(s->i_s, rx, n);
    s->i_s_len = n;
    p = 17;
    bool guess_correct = true;
    for (int i = 0; i < 10; i++) {
        const uint8_t *list;
        size_t len;
        if (!buf_get_str(s->i_s, s->i_s_len, &p, &list, &len)) return false;
        if (i == 0) {
            if (!sshd_name_in_list(list, len, "curve25519-sha256") &&
                !sshd_name_in_list(list, len, "curve25519-sha256@libssh.org")) return false;
        } else if (i < 8 && !sshd_name_in_list(list, len, names[i])) return false;
        if (i < 2) {
            size_t first = 0;
            while (first < len && list[first] != ',') ++first;
            if (i == 0) {
                if (!sshd_string_eq(list, first, "curve25519-sha256") &&
                    !sshd_string_eq(list, first, "curve25519-sha256@libssh.org")) guess_correct = false;
            } else if (!sshd_string_eq(list, first, names[i])) guess_correct = false;
        }
    }
    if (p + 5 != s->i_s_len) return false;
    if (s->i_s[p] && !guess_correct && ssh_recv_packet(s, &rx, 15000) <= 0) return false;
    n = ssh_recv_packet(s, &rx, 15000);
    p = 1;
    const uint8_t *q;
    size_t qn;
    uint8_t client_q[32];
    if (n <= 0 || rx[0] != SSH_MSG_KEX_ECDH_INIT ||
        !buf_get_str(rx, n, &p, &q, &qn) || qn != 32 || p != (size_t)n) return false;
    memcpy(client_q, q, 32);
    uint8_t priv[32], server_q[32], raw_k[32], k_mpint[37];
    static const uint8_t base[32] = {9};
    ssh_random_bytes(priv, 32);
    x25519_scalarmult(server_q, priv, base);
    x25519_scalarmult(raw_k, priv, client_q);
    memset(priv, 0, sizeof(priv));
    size_t first = 0;
    while (first < 32 && !raw_k[first]) first++;
    if (first == 32) return false;
    size_t kl = 4;
    if (raw_k[first] & 0x80) k_mpint[kl++] = 0;
    memcpy(k_mpint + kl, raw_k + first, 32 - first);
    kl += 32 - first;
    put_u32_be(k_mpint, kl - 4);
    uint8_t host_blob[51];
    size_t hn = buf_put_str(host_blob, 0, "ssh-ed25519");
    hn = buf_put_mem(host_blob, hn, s_sshd_host_pub, 32);
    sha256_ctx_t hash;
    uint8_t h[32];
    sha256_init(&hash);
    sha256_update_ssh_str(&hash, s->v_s, vn);
    sha256_update_ssh_str(&hash, SSHD_BANNER, strlen(SSHD_BANNER));
    sha256_update_ssh_str(&hash, s->i_s, s->i_s_len);
    sha256_update_ssh_str(&hash, s->i_c, s->i_c_len);
    sha256_update_ssh_str(&hash, host_blob, hn);
    sha256_update_ssh_str(&hash, client_q, 32);
    sha256_update_ssh_str(&hash, server_q, 32);
    sha256_update(&hash, k_mpint, kl);
    sha256_final(&hash, h);
    memcpy(s->session_id, h, 32);
    uint8_t sig[64], sig_blob[83];
    ed25519_sign(sig, h, 32, s_sshd_host_seed, s_sshd_host_pub);
    size_t sn = buf_put_str(sig_blob, 0, "ssh-ed25519");
    sn = buf_put_mem(sig_blob, sn, sig, sizeof(sig));
    p = buf_put_u8(s->payload, 0, SSH_MSG_KEX_ECDH_REPLY);
    p = buf_put_mem(s->payload, p, host_blob, hn);
    p = buf_put_mem(s->payload, p, server_q, 32);
    p = buf_put_mem(s->payload, p, sig_blob, sn);
    if (!ssh_send_packet(s, s->payload, p)) return false;
    s->payload[0] = SSH_MSG_NEWKEYS;
    if (!ssh_send_packet(s, s->payload, 1)) return false;
    n = ssh_recv_packet(s, &rx, 15000);
    if (n != 1 || rx[0] != SSH_MSG_NEWKEYS) return false;
    uint8_t iv_tx[32], iv_rx[32], key_tx[32], key_rx[32];
    ssh_derive_key(k_mpint, kl, h, h, 'B', iv_tx);
    ssh_derive_key(k_mpint, kl, h, h, 'A', iv_rx);
    ssh_derive_key(k_mpint, kl, h, h, 'D', key_tx);
    ssh_derive_key(k_mpint, kl, h, h, 'C', key_rx);
    ssh_derive_key(k_mpint, kl, h, h, 'F', s->mac_key_c2s);
    ssh_derive_key(k_mpint, kl, h, h, 'E', s->mac_key_s2c);
    aes128_init(&s->enc_c2s, key_tx, iv_tx);
    aes128_init(&s->dec_s2c, key_rx, iv_rx);
    memset(raw_k, 0, sizeof(raw_k));
    memset(k_mpint, 0, sizeof(k_mpint));
    memset(key_tx, 0, sizeof(key_tx));
    memset(key_rx, 0, sizeof(key_rx));
    s->encrypted = true;
    return true;
}

static bool sshd_authenticate(ssh_session_t *s)
{
    uint8_t *rx;
    int n = ssh_recv_packet(s, &rx, 15000);
    size_t pos = 1, len;
    const uint8_t *value;
    if (n <= 0 || rx[0] != SSH_MSG_SERVICE_REQUEST ||
        !buf_get_str(rx, n, &pos, &value, &len) ||
        !sshd_string_eq(value, len, "ssh-userauth")) return false;
    pos = buf_put_u8(s->payload, 0, SSH_MSG_SERVICE_ACCEPT);
    pos = buf_put_str(s->payload, pos, "ssh-userauth");
    if (!ssh_send_packet(s, s->payload, pos)) return false;
    for (int tries = 0; tries < 8 && s_sshd_run; tries++) {
        n = ssh_recv_packet(s, &rx, 30000);
        if (n <= 0 || rx[0] != SSH_MSG_USERAUTH_REQUEST) return false;
        const uint8_t *user, *service, *method, *password;
        size_t un, sn, mn, pn;
        pos = 1;
        if (!buf_get_str(rx, n, &pos, &user, &un) ||
            !buf_get_str(rx, n, &pos, &service, &sn) ||
            !buf_get_str(rx, n, &pos, &method, &mn)) return false;
        bool ok = false;
        if (sshd_string_eq(service, sn, "ssh-connection") &&
            sshd_string_eq(method, mn, "password") && pos < (size_t)n && rx[pos++] == 0 &&
            buf_get_str(rx, n, &pos, &password, &pn) && pos == (size_t)n && pn <= 64) {
            uint8_t digest[32];
            sha256_ctx_t hash;
            sha256_init(&hash);
            sha256_update(&hash, password, pn);
            sha256_final(&hash, digest);
            ok = sshd_string_eq(user, un, s_sshd_user) &&
                 sshd_bytes_equal(digest, s_sshd_password_hash, 32);
            memset(digest, 0, sizeof(digest));
            memset((void *)password, 0, pn);
        }
        if (ok) {
            s->payload[0] = SSH_MSG_USERAUTH_SUCCESS;
            return ssh_send_packet(s, s->payload, 1);
        }
        task_sleep(300);
        pos = buf_put_u8(s->payload, 0, SSH_MSG_USERAUTH_FAILURE);
        pos = buf_put_str(s->payload, pos, "password");
        pos = buf_put_u8(s->payload, pos, 0);
        if (!ssh_send_packet(s, s->payload, pos)) return false;
    }
    return false;
}

static bool sshd_channel_reply(ssh_session_t *s, uint8_t type)
{
    size_t p = buf_put_u8(s->payload, 0, type);
    p = buf_put_u32(s->payload, p, s->remote_chan);
    return ssh_send_packet(s, s->payload, p);
}

static bool sshd_channel_output(ssh_session_t *s, const char *data, size_t len)
{
    size_t p = buf_put_u8(s->payload, 0, SSH_MSG_CHANNEL_DATA);
    p = buf_put_u32(s->payload, p, s->remote_chan);
    p = buf_put_mem(s->payload, p, data, len);
    if (!ssh_send_packet(s, s->payload, p)) return false;
    s->remote_win -= len;
    return true;
}

static void sshd_console(ssh_session_t *s)
{
    bool opened = false, shell = false, pty=false, peer_eof=false;
    uint64_t eof_at=0;
    sftp_server_t *storage=NULL;
    uint32_t remote_max = 0, local_window = SSHD_INPUT_WINDOW;
    uint32_t log_pos = kconsole_log_end();
    char output[512];
    char pending[SSHD_INPUT_WINDOW];
    size_t pending_pos = 0, pending_len = 0;
    uint64_t started = hal_time_now_us();
    while (s_sshd_run && hal_time_now_us() - started < SSHD_SESSION_LIMIT_US) {
        if(peer_eof && hal_time_now_us()-eof_at > 2000000u) break;
        if (pending_pos < pending_len) {
            size_t n = storage?sftp_server_feed(storage,pending+pending_pos,pending_len-pending_pos):
                kconsole_inject_n(pending + pending_pos, pending_len - pending_pos);
            pending_pos += n;
            local_window += n;
            if (n) {
                size_t p = buf_put_u8(s->payload, 0, SSH_MSG_CHANNEL_WINDOW_ADJUST);
                p = buf_put_u32(s->payload, p, s->remote_chan);
                p = buf_put_u32(s->payload, p, n);
                if (!ssh_send_packet(s, s->payload, p)) break;
            }
        }
        if (!peer_eof && (shell || storage) && s->remote_win) {
            size_t cap = sizeof(output);
            if (cap > s->remote_win) cap = s->remote_win;
            if (cap > remote_max) cap = remote_max;
            size_t n = storage?sftp_server_read(storage,output,cap):kconsole_log_read(&log_pos, output, cap);
            if (n && !sshd_channel_output(s, output, n)) break;
        }
        if(storage&&sftp_server_failed(storage))break;
        uint8_t *rx;
        int n = ssh_recv_packet(s, &rx, 10);
        if (n < 0) break;
        if (!n) { task_sleep(2); continue; }
        size_t pos = 1;
        uint32_t recipient;
        if (rx[0] == SSH_MSG_GLOBAL_REQUEST) {
            const uint8_t *name;
            size_t len;
            if (!buf_get_str(rx, n, &pos, &name, &len) || pos >= (size_t)n) break;
            if (rx[pos]) {
                s->payload[0] = SSH_MSG_REQUEST_FAILURE;
                if (!ssh_send_packet(s, s->payload, 1)) break;
            }
            continue;
        }
        if (rx[0] == SSH_MSG_CHANNEL_OPEN) {
            const uint8_t *name;
            size_t len;
            uint32_t sender, window, max_packet;
            if (!buf_get_str(rx, n, &pos, &name, &len) ||
                !buf_get_u32(rx, n, &pos, &sender) ||
                !buf_get_u32(rx, n, &pos, &window) ||
                !buf_get_u32(rx, n, &pos, &max_packet)) break;
            if (opened || !sshd_string_eq(name, len, "session") || !max_packet) {
                size_t p = buf_put_u8(s->payload, 0, SSH_MSG_CHANNEL_OPEN_FAILURE);
                p = buf_put_u32(s->payload, p, sender);
                p = buf_put_u32(s->payload, p, 1);
                p = buf_put_str(s->payload, p, "One console or SFTP channel is supported");
                p = buf_put_str(s->payload, p, "");
                if (!ssh_send_packet(s, s->payload, p)) break;
                continue;
            }
            s->remote_chan = sender;
            s->remote_win = window;
            remote_max = max_packet;
            size_t p = buf_put_u8(s->payload, 0, SSH_MSG_CHANNEL_OPEN_CONFIRMATION);
            p = buf_put_u32(s->payload, p, sender);
            p = buf_put_u32(s->payload, p, 0);
            p = buf_put_u32(s->payload, p, SSHD_INPUT_WINDOW);
            p = buf_put_u32(s->payload, p, SSHD_INPUT_WINDOW);
            if (!ssh_send_packet(s, s->payload, p)) break;
            opened = true;
            continue;
        }
        if (!opened || !buf_get_u32(rx, n, &pos, &recipient) || recipient != 0) break;
        if (rx[0] == SSH_MSG_CHANNEL_WINDOW_ADJUST) {
            uint32_t add;
            if (!buf_get_u32(rx, n, &pos, &add) || UINT32_MAX - s->remote_win < add) break;
            s->remote_win += add;
        } else if (rx[0] == SSH_MSG_CHANNEL_REQUEST) {
            const uint8_t *name;
            size_t len;
            if (!buf_get_str(rx, n, &pos, &name, &len) || pos >= (size_t)n) break;
            bool reply = rx[pos++], ok = false;
            if (sshd_string_eq(name, len, "pty-req")) {
                const uint8_t *term, *modes;
                size_t tn, mode_n;
                uint32_t cols, rows, width, height;
                ok = !shell && !storage && buf_get_str(rx, n, &pos, &term, &tn) &&
                     buf_get_u32(rx, n, &pos, &cols) && buf_get_u32(rx, n, &pos, &rows) &&
                     buf_get_u32(rx, n, &pos, &width) && buf_get_u32(rx, n, &pos, &height) &&
                     buf_get_str(rx, n, &pos, &modes, &mode_n) && pos == (size_t)n;
                if(ok)pty=true;
            } else if(sshd_string_eq(name,len,"subsystem")) {
                const uint8_t *subsystem;size_t subsystem_len;
                ok=!shell&&!storage&&!pty&&s_sshd_storage_enabled&&
                    buf_get_str(rx,n,&pos,&subsystem,&subsystem_len)&&pos==(size_t)n&&
                    sshd_string_eq(subsystem,subsystem_len,"sftp");
                if(ok){storage=sftp_server_create(s_sshd_storage_writable);ok=storage!=NULL;}
            } else if (sshd_string_eq(name, len, "shell")) {
                ok = !shell && !storage && pos == (size_t)n;
                if (ok) { shell = true; log_pos = kconsole_log_end(); }
            } else if (sshd_string_eq(name, len, "window-change")) {
                /* Shared console dimensions remain the LCD dimensions. */
                ok = shell && pos + 16 == (size_t)n;
            }
            if (reply && !sshd_channel_reply(s, ok ? SSH_MSG_CHANNEL_SUCCESS : SSH_MSG_CHANNEL_FAILURE)) break;
            if (ok && sshd_string_eq(name, len, "shell")) {
                const char message[] = "\r\nMeOS shared console. Local and remote input share one shell.\r\n";
                size_t remaining = sizeof(message) - 1, at = 0;
                while (remaining && s->remote_win) {
                    size_t chunk = remaining;
                    if (chunk > s->remote_win) chunk = s->remote_win;
                    if (chunk > remote_max) chunk = remote_max;
                    if (!sshd_channel_output(s, message + at, chunk)) return;
                    at += chunk;
                    remaining -= chunk;
                }
                kconsole_inject("\r");
            }
        } else if (rx[0] == SSH_MSG_CHANNEL_DATA) {
            const uint8_t *data;
            size_t len;
            if (peer_eof || (!shell && !storage) || !buf_get_str(rx, n, &pos, &data, &len) || pos != (size_t)n ||
                len > local_window || len > sizeof(pending)) break;
            /* Preserve already queued input before accepting the next packet. */
            size_t left = pending_len - pending_pos;
            if (left + len > sizeof(pending)) break;
            if (left) memmove(pending, pending + pending_pos, left);
            memcpy(pending + left, data, len);
            pending_len = left + len;
            pending_pos = 0;
            local_window -= len;
        } else if (rx[0] == SSH_MSG_CHANNEL_CLOSE) {
            sshd_channel_reply(s, SSH_MSG_CHANNEL_CLOSE);
            break;
        } else if (rx[0] == SSH_MSG_CHANNEL_EOF) {
            /* EOF ends input, not the channel. Allow the peer to finish CLOSE. */
            if(pos!=(size_t)n || peer_eof)break;
            peer_eof=true; eof_at=hal_time_now_us();
            if(!sshd_channel_reply(s, SSH_MSG_CHANNEL_EOF))break;
        } else {
            break;
        }
    }
    if(storage){int close_error=sftp_server_destroy(storage);if(close_error<0)kprintf("sshd: SFTP cleanup failed (%d)\n",close_error);}
}

static void sshd_task(void *arg)
{
    (void)arg;
    ssh_session_t *s = kmalloc_psram(sizeof(*s));
    if (!s) s = kmalloc(sizeof(*s));
    if (!s) { s_sshd_run = false; s_sshd_task_live = false; return; }
    while (s_sshd_run) {
        wifi_status_t network;
        wifi_get_network_status(&network);
        if (!network.ip[0] || !wifi_tcp_listen_sock(SSHD_SOCK, s_sshd_port)) {
            task_sleep(200);
            continue;
        }
        bool accepted = false;
        uint64_t accept_deadline = hal_time_now_us() + 30000000;
        while (s_sshd_run && hal_time_now_us() < accept_deadline) {
            if (wifi_tcp_accept_sock(SSHD_SOCK, 100)) { accepted = true; break; }
            task_sleep(10);
        }
        if (!s_sshd_run) break;
        if (!accepted) { wifi_tcp_close_sock(SSHD_SOCK); continue; }
        memset(s, 0, sizeof(*s));
        s->sock = SSHD_SOCK;
        if (sshd_handshake(s) && sshd_authenticate(s)) {
            s_sshd_connected = true;
            sshd_console(s);
            s_sshd_connected = false;
        }
        wifi_tcp_close_sock(SSHD_SOCK);
        memset(s, 0, sizeof(*s));
        task_sleep(100);
    }
    wifi_tcp_close_sock(SSHD_SOCK);
    memset(s, 0, sizeof(*s));
    kfree(s);
    memset(s_sshd_password_hash, 0, sizeof(s_sshd_password_hash));
    memset(s_sshd_host_seed, 0, sizeof(s_sshd_host_seed));
    s_sshd_connected = false;
    s_sshd_task_live = false;
}

int sshd_cmd_main(int argc, char **argv)
{
    if(argc==3&&!strcmp(argv[1],"storage-test"))return sftp_storage_selftest(argv[2])?0:1;
    if (argc == 1 || (argc == 2 && !strcmp(argv[1], "status"))) {
        kprintf("sshd: %s port=%u session=%s socket=%d storage=%s\n",
                s_sshd_run ? "running" : "stopped", s_sshd_port,
                s_sshd_connected ? "authenticated" : "none", SSHD_SOCK,
                !s_sshd_storage_enabled?"off":s_sshd_storage_writable?"read-write":"read-only");
        return 0;
    }
    if(argc==3&&!strcmp(argv[1],"storage")) {
        if(s_sshd_task_live){kputs("sshd: stop the server before changing storage policy\n");return 1;}
        if(strcmp(argv[2],"off")&&strcmp(argv[2],"ro")&&strcmp(argv[2],"rw")){kputs("usage: sshd storage ro|rw|off\n");return 1;}
        s_sshd_storage_enabled=strcmp(argv[2],"off")!=0;
        s_sshd_storage_writable=!strcmp(argv[2],"rw");
        kputs("sshd: storage policy updated (applies to the next start)\n");return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "stop")) {
        s_sshd_run = false;
        wifi_tcp_close_sock(SSHD_SOCK);
        kputs("sshd: stopping\n");
        return 0;
    }
    if ((argc != 4 && argc != 5) || strcmp(argv[1], "start") ||
        !argv[2][0] || strlen(argv[2]) > 32 || strlen(argv[3]) < 8 || strlen(argv[3]) > 64) {
        kputs("usage: sshd start USER PASSWORD [PORT] | status | stop | storage ro|rw|off\n"
              "Password: 8..64 bytes. One authenticated shared console OR SFTP channel; no exec.\n");
        return 1;
    }
    if (s_sshd_task_live) { kputs("sshd: already running or stopping\n"); return 1; }
    unsigned port = 22;
    if (argc == 5) {
        port = 0;
        for (const char *p = argv[4]; *p; p++) {
            if (*p < '0' || *p > '9' || port > 6553) { port = 0; break; }
            port = port * 10 + *p - '0';
        }
        if (!port || port > 65535) { kputs("sshd: invalid port\n"); return 1; }
    }
#if !M5OS_BOARD_TAB5
    kputs("sshd: hardware entropy support is currently required on Tab5\n");
    return 1;
#endif
    if (!ssh_rng_init()) { kputs("sshd: hardware entropy unavailable\n"); return 1; }
    if (!sshd_hostkey_load()) { kputs("sshd: host key unavailable\n"); return 1; }
    memcpy(s_sshd_user, argv[2], strlen(argv[2]) + 1);
    sha256_ctx_t hash;
    sha256_init(&hash);
    sha256_update(&hash, argv[3], strlen(argv[3]));
    sha256_final(&hash, s_sshd_password_hash);
    memset(argv[3], 0, strlen(argv[3]));
    s_sshd_port = port;
    s_sshd_run = s_sshd_task_live = true;
    if (task_create_stack("sshd", sshd_task, NULL, TASK_PRIO_DEFAULT, 16384) < 0) {
        s_sshd_run = s_sshd_task_live = false;
        memset(s_sshd_password_hash, 0, sizeof(s_sshd_password_hash));
        kputs("sshd: cannot create task\n");
        return 1;
    }
    kprintf("sshd: starting port=%u user=%s; host key %s\n", port, s_sshd_user, SSHD_HOSTKEY_PATH);
    return 0;
}
