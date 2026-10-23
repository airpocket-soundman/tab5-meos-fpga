/* Minimal ESP-Hosted-NG inspection path. No Wi-Fi association or flash writes.
 * Protocol reference: espressif/esp-hosted legacy/fg-ng adapter.h and esp_cmd.c.
 * This implementation uses wire fields directly, independent of Linux driver code.
 */
static struct {
    bool valid, version_valid, reply;
    uint8_t chip, capabilities, version[5], pending, response_status;
    char project[4];
    uint32_t reset_reason;
    uint32_t rx_buffer_size, received_frames, last_token;
    uint16_t last_frame_len, last_frame_offset;
    uint8_t last_interface, last_packet_type, last_command, last_status;
    uint8_t mac[6];
} s_ng;

static uint32_t ng_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void ng_capture_boot(const uint8_t *frame)
{
    const uint8_t *event = frame + hosted_wire_u16(frame + 6);
    size_t len = hosted_wire_u16(frame + 4);
    mem_zero(&s_ng, sizeof s_ng);
    s_ng.valid = true;
    for (size_t at = 8; at + 2 <= len;) {
        uint8_t tag = event[at++], count = event[at++];
        if (count > len - at) break; /* caller validated complete startup TLVs */
        if (tag == 3 && count == 1) s_ng.chip = event[at];
        if (tag == 0 && count == 1) s_ng.capabilities = event[at];
        if (tag == 1 && count == 12) {
            for (unsigned i = 0; i < 3; i++) {
                uint8_t c = event[at + i];
                if (!c) break;
                s_ng.project[i] = c >= 32 && c <= 126 ? (char)c : '?';
            }
            mem_copy(s_ng.version, event + at + 3, 5);
            s_ng.reset_reason = ng_u32(event + at + 8);
            s_ng.version_valid = true;
        }
        if (tag == 5 && count == 4) s_ng.rx_buffer_size = ng_u32(event + at);
        at += count;
    }
}

static void ng_receive_frames(const uint8_t *frames, size_t size)
{
    size_t pos = 0;
    while (size - pos >= 12) {
        const uint8_t *f = frames + pos;
        size_t len = hosted_wire_u16(f + 4), off = hosted_wire_u16(f + 6);
        s_ng.received_frames++;
        s_ng.last_interface = f[0]; s_ng.last_packet_type = f[2];
        s_ng.last_frame_len = (uint16_t)len; s_ng.last_frame_offset = (uint16_t)off;
        if (off < 12 || off > 16 || off > size - pos || len > size - pos - off) break;
        bool valid = true;
        if (s_ng.capabilities & 0x80u) {
            uint16_t supplied = hosted_wire_u16(f + 8);
            uint16_t sum = compute_checksum(f, off + len);
            sum = (uint16_t)(sum - f[8] - f[9]);
            valid = sum == supplied;
        }
        if (valid && f[0] == 0 && f[2] == 2 && len >= 8) {
            const uint8_t *cmd = f + off;
            s_ng.last_command = cmd[0]; s_ng.last_status = cmd[1];
            if (s_ng.pending && cmd[0] == s_ng.pending) {
                s_ng.response_status = cmd[1];
                if (cmd[0] == 3 && cmd[1] == 2 && len >= 14)
                    mem_copy(s_ng.mac, cmd + 8, 6);
                s_ng.reply = true;
            }
        }
        pos += off + len;
    }
}

/* Build a single NG command; callers serialize requests under s_wifi_lock. */
static bool ng_send_command(uint8_t code, const uint8_t *body, size_t body_len)
{
    if (body_len > 1018u || (body_len && !body)) return false;
    size_t total = 20u + body_len;
    /* Match official NG SDIO packet framing: the advertised CMD53 length
     * includes the complete padded block, unlike MCU streaming transfers. */
    size_t block = (total / SDIO_BLOCK_SIZE + 1u) * SDIO_BLOCK_SIZE;
    uint32_t token = 0;
    bool free_buffer = false;
    for (unsigned i = 0; i < 200; i++) {
        if (sdio_reg_read(ESP_SLAVE_TOKEN_RDATA, &token, 4)) {
            uint32_t available = (((token >> 16) & 0xfffu) + 0x1000u - s_sdio_tx_bufs) & 0xfffu;
            if (available) { free_buffer = true; break; }
        }
        esp_rom_delay_us(500);
    }
    if (!free_buffer) return false;
    s_ng.last_token = token;
    mem_zero(s_tx_packet_buf, block);
    /* NG: STA interface 0; packet_type=COMMAND_REQUEST; eight-byte command. */
    s_tx_packet_buf[2] = 1;
    s_tx_packet_buf[4] = (uint8_t)(8u + body_len);
    s_tx_packet_buf[5] = (uint8_t)((8u + body_len) >> 8);
    s_tx_packet_buf[6] = 12;
    s_tx_packet_buf[12] = code;
    if (body_len) mem_copy(s_tx_packet_buf + 20, body, body_len);
    if (s_ng.capabilities & 0x80u) {
        uint16_t sum = compute_checksum(s_tx_packet_buf, total);
        s_tx_packet_buf[8] = (uint8_t)sum;
        s_tx_packet_buf[9] = (uint8_t)(sum >> 8);
    }
    if (!sdio_cmd53(true, 1, ESP_SLAVE_CMD53_END - (uint32_t)block, true,
                    s_tx_packet_buf, block)) return false;
    s_sdio_tx_bufs = (s_sdio_tx_bufs + 1) & 0xfffu;
    return true;
}

/* No automatic retry: an OTA write is not idempotent after a lost response. */
static bool ng_command_locked(uint8_t code, const uint8_t *body, size_t size,
                              uint32_t timeout_ms)
{
    uint32_t rx_before = s_sdio_rx_bytes, frames_before = s_ng.received_frames;
    s_ng.reply = false;
    s_ng.pending = code;
    s_ng.response_status = 0xff;
    if (!ng_send_command(code, body, size)) {
        s_ng.pending = 0;
        kprintf("C6 NG: command %u send failed\n", code);
        return false;
    }
    uint64_t end = hal_time_now_us() + (uint64_t)timeout_ms * 1000u;
    while (!s_ng.reply && hal_time_now_us() < end) {
        sdio_poll_rx_locked();
        if (!s_ng.reply) task_sleep(5);
    }
    s_ng.pending = 0;
    if (!s_ng.reply) {
        uint32_t token_after = 0, pending_bytes = 0;
        bool token_ok = sdio_reg_read(ESP_SLAVE_TOKEN_RDATA, &token_after, 4);
        bool pkt_ok = sdio_reg_read(ESP_SLAVE_PKT_LEN_REG, &pending_bytes, 4);
        kprintf("C6 NG: command %u timeout rx_delta=%u frames=%u token=%08x/%08x token_ok=%u packet_counter=%u packet_ok=%u\n",
                code, (unsigned)((s_sdio_rx_bytes - rx_before) & 0xfffffu),
                (unsigned)(s_ng.received_frames - frames_before),
                (unsigned)s_ng.last_token, (unsigned)token_after, (unsigned)token_ok,
                (unsigned)(pending_bytes & 0xfffffu), (unsigned)pkt_ok);
        if (s_ng.received_frames != frames_before)
            kprintf("C6 NG: last RX interface=%u packet_type=%u len=%u offset=%u command=%u status=%u\n",
                    s_ng.last_interface, s_ng.last_packet_type, s_ng.last_frame_len,
                    s_ng.last_frame_offset, s_ng.last_command, s_ng.last_status);
        return false;
    }
    if (s_ng.response_status != 2) {
        kprintf("C6 NG: command %u status=%u (success=2, unsupported=4)\n",
                code, s_ng.response_status);
        return false;
    }
    return true;
}

static bool ng_probe_get_mac_locked(void)
{
    bool ok = ng_command_locked(3, NULL, 0, 2000);
    if (ok) {
        kprintf("C6 NG: GET_MAC success, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
                s_ng.mac[0], s_ng.mac[1], s_ng.mac[2],
                s_ng.mac[3], s_ng.mac[4], s_ng.mac[5]);
    }
    return ok;
}

/* OTA was introduced in official NG 1.0.3.0.1 (commit 85d103a5, 2024-12-06).
 * Earlier advertised versions are rejected without sending OTA_START. */
static inline bool ng_ota_version_supported(void)
{
    static const uint8_t minimum[5] = {1, 0, 3, 0, 1};
    if (!s_ng.valid || !s_ng.version_valid) return false;
    for (unsigned i = 0; i < 5; i++) {
        if (s_ng.version[i] != minimum[i]) return s_ng.version[i] > minimum[i];
    }
    return true;
}
