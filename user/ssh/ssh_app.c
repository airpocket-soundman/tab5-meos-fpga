/*
 * M5OS SSH-2 Client (RFC 4253 / 4252 / 4254 / 8731)
 *
 * Self-contained SSH-2 implementation for M5Stack Tab5:
 *  - Transport  : SSH-2.0 binary packet protocol over M5OS TCP/IP + DNS
 *  - KEX        : curve25519-sha256, curve25519-sha256@libssh.org (X25519 + SHA-256)
 *  - Host Key   : ssh-ed25519, ecdsa-sha2-nistp256, rsa-sha2-256, rsa-sha2-512, ssh-rsa
 *  - Cipher     : aes128-ctr
 *  - MAC        : hmac-sha2-256
 *  - Userauth   : password, keyboard-interactive (PAM)
 *  - Connection : interactive PTY shell (vt100 80x45) or single-command exec
 */
#include "ssh_app.h"
#include "../tui/dqwin.h"
#include "../../drivers/lcd/fbcon.h"
#include <m5os/wifi.h>
#include <m5os/vfs.h>
#include <m5os/kmalloc.h>
#include <m5os/kprintf.h>
#include <m5os/hal_time.h>
#include <m5os/task.h>
#include <m5os/errno.h>
#include <string.h>

#define SSH_CFG_PATH       "/home/ssh.cfg"
#define SSH_BANNER_CLIENT  "SSH-2.0-MeOS_1.0"
#define SSH_MAX_PKT        4608

/* SSH-2 Message Numbers (RFC 4253 / 4252 / 4254) */
#define SSH_MSG_DISCONNECT                1
#define SSH_MSG_IGNORE                    2
#define SSH_MSG_UNIMPLEMENTED             3
#define SSH_MSG_DEBUG                     4
#define SSH_MSG_SERVICE_REQUEST           5
#define SSH_MSG_SERVICE_ACCEPT            6
#define SSH_MSG_EXT_INFO                  7
#define SSH_MSG_KEXINIT                   20
#define SSH_MSG_NEWKEYS                   21
#define SSH_MSG_KEX_ECDH_INIT             30
#define SSH_MSG_KEX_ECDH_REPLY            31
#define SSH_MSG_USERAUTH_REQUEST          50
#define SSH_MSG_USERAUTH_FAILURE          51
#define SSH_MSG_USERAUTH_SUCCESS          52
#define SSH_MSG_USERAUTH_BANNER           53
#define SSH_MSG_USERAUTH_INFO_REQUEST     60
#define SSH_MSG_USERAUTH_INFO_RESPONSE    61
#define SSH_MSG_GLOBAL_REQUEST            80
#define SSH_MSG_REQUEST_SUCCESS           81
#define SSH_MSG_REQUEST_FAILURE           82
#define SSH_MSG_CHANNEL_OPEN              90
#define SSH_MSG_CHANNEL_OPEN_CONFIRMATION 91
#define SSH_MSG_CHANNEL_OPEN_FAILURE      92
#define SSH_MSG_CHANNEL_WINDOW_ADJUST     93
#define SSH_MSG_CHANNEL_DATA              94
#define SSH_MSG_CHANNEL_EXTENDED_DATA     95
#define SSH_MSG_CHANNEL_EOF               96
#define SSH_MSG_CHANNEL_CLOSE             97
#define SSH_MSG_CHANNEL_REQUEST           98
#define SSH_MSG_CHANNEL_SUCCESS           99
#define SSH_MSG_CHANNEL_FAILURE           100

/* =========================================================================
 * 1. Cryptographic Primitives: SHA-256, HMAC-SHA-256, X25519, AES-128-CTR
 * ========================================================================= */

typedef struct {
    uint32_t state[8];
    uint64_t bitcount;
    uint8_t  buf[64];
} sha256_ctx_t;

static const uint32_t K256[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

static inline uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void sha256_transform(uint32_t state[8], const uint8_t block[64])
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + K256[i] + w[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void sha256_init(sha256_ctx_t *ctx)
{
    ctx->state[0] = 0x6a09e667u; ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u; ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu; ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu; ctx->state[7] = 0x5be0cd19u;
    ctx->bitcount = 0;
}

static void sha256_update(sha256_ctx_t *ctx, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t idx = (size_t)((ctx->bitcount >> 3) & 0x3Fu);
    ctx->bitcount += (uint64_t)len << 3;
    for (size_t i = 0; i < len; i++) {
        ctx->buf[idx++] = p[i];
        if (idx == 64) {
            sha256_transform(ctx->state, ctx->buf);
            idx = 0;
        }
    }
}

static void sha256_final(sha256_ctx_t *ctx, uint8_t out[32])
{
    size_t idx = (size_t)((ctx->bitcount >> 3) & 0x3Fu);
    ctx->buf[idx++] = 0x80u;
    if (idx > 56) {
        while (idx < 64) ctx->buf[idx++] = 0;
        sha256_transform(ctx->state, ctx->buf);
        idx = 0;
    }
    while (idx < 56) ctx->buf[idx++] = 0;
    for (int i = 0; i < 8; i++) {
        ctx->buf[56 + i] = (uint8_t)(ctx->bitcount >> (56 - i * 8));
    }
    sha256_transform(ctx->state, ctx->buf);
    for (int i = 0; i < 8; i++) {
        out[i * 4]     = (uint8_t)(ctx->state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

static void hmac_sha256_packet(const uint8_t key[32], uint32_t seq,
                               const uint8_t *pkt, size_t pkt_len, uint8_t out[32])
{
    uint8_t k_ipad[64], k_opad[64], seq_be[4], inner[32];
    for (int i = 0; i < 32; i++) {
        k_ipad[i] = key[i] ^ 0x36u;
        k_opad[i] = key[i] ^ 0x5cu;
    }
    for (int i = 32; i < 64; i++) {
        k_ipad[i] = 0x36u;
        k_opad[i] = 0x5cu;
    }
    seq_be[0] = (uint8_t)(seq >> 24);
    seq_be[1] = (uint8_t)(seq >> 16);
    seq_be[2] = (uint8_t)(seq >> 8);
    seq_be[3] = (uint8_t)(seq);

    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, k_ipad, 64);
    sha256_update(&ctx, seq_be, 4);
    sha256_update(&ctx, pkt, pkt_len);
    sha256_final(&ctx, inner);

    sha256_init(&ctx);
    sha256_update(&ctx, k_opad, 64);
    sha256_update(&ctx, inner, 32);
    sha256_final(&ctx, out);
}

/* --- X25519 (RFC 7748) --- */
typedef int64_t gf[16];

static void car25519(gf o)
{
    for (int i = 0; i < 16; i++) {
        o[i] += (1LL << 16);
        int64_t c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}

static void sel25519(gf p, gf q, int b)
{
    int64_t c = ~(b - 1);
    for (int i = 0; i < 16; i++) {
        int64_t t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void pack25519(uint8_t *o, const gf n)
{
    gf m, t;
    for (int i = 0; i < 16; i++) t[i] = n[i];
    car25519(t); car25519(t); car25519(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) {
        o[2 * i]     = (uint8_t)(t[i] & 0xff);
        o[2 * i + 1] = (uint8_t)(t[i] >> 8);
    }
}

static void unpack25519(gf o, const uint8_t *n)
{
    for (int i = 0; i < 16; i++) o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
    o[15] &= 0x7fff;
}

static void A25519(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] + b[i]; }
static void Z25519(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] - b[i]; }

static void M25519(gf o, const gf a, const gf b)
{
    int64_t t[31];
    for (int i = 0; i < 31; i++) t[i] = 0;
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++)
            t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; i++) o[i] = t[i];
    car25519(o); car25519(o);
}

static void S25519(gf o, const gf a) { M25519(o, a, a); }

static void inv25519(gf o, const gf i)
{
    gf c;
    for (int a = 0; a < 16; a++) c[a] = i[a];
    for (int a = 253; a >= 0; a--) {
        S25519(c, c);
        if (a != 2 && a != 4) M25519(c, c, i);
    }
    for (int a = 0; a < 16; a++) o[a] = c[a];
}

static void x25519_scalarmult(uint8_t *q, const uint8_t *n, const uint8_t *p)
{
    uint8_t z[32];
    gf x, a, b, c, d, e, f;
    static const gf _121665 = {0xDB41, 1};
    for (int i = 0; i < 31; i++) z[i] = n[i];
    z[31] = (n[31] & 127) | 64;
    z[0] &= 248;
    unpack25519(x, p);
    for (int i = 0; i < 16; i++) { b[i] = x[i]; d[i] = a[i] = c[i] = 0; }
    a[0] = d[0] = 1;
    for (int i = 254; i >= 0; --i) {
        int r = (z[i >> 3] >> (i & 7)) & 1;
        sel25519(a, b, r);
        sel25519(c, d, r);
        A25519(e, a, c);
        Z25519(a, a, c);
        A25519(c, b, d);
        Z25519(b, b, d);
        S25519(d, e);
        S25519(f, a);
        M25519(a, c, a);
        M25519(c, b, e);
        A25519(e, a, c);
        Z25519(a, a, c);
        S25519(b, a);
        Z25519(c, d, f);
        M25519(a, c, _121665);
        A25519(a, a, d);
        M25519(c, c, a);
        M25519(a, d, f);
        M25519(d, b, x);
        S25519(b, e);
        sel25519(a, b, r);
        sel25519(c, d, r);
    }
    inv25519(c, c);
    M25519(a, a, c);
    pack25519(q, a);
}

/* --- AES-128-CTR --- */
static const uint8_t s_aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

typedef struct {
    uint8_t rk[176];
    uint8_t iv[16];
} aes128_ctr_ctx_t;

static void aes128_init(aes128_ctr_ctx_t *ctx, const uint8_t key[16], const uint8_t iv[16])
{
    memcpy(ctx->rk, key, 16);
    memcpy(ctx->iv, iv, 16);
    uint8_t rcon = 1;
    for (int i = 4; i < 44; i++) {
        uint8_t t[4];
        memcpy(t, &ctx->rk[(i - 1) * 4], 4);
        if ((i & 3) == 0) {
            uint8_t u = t[0];
            t[0] = s_aes_sbox[t[1]] ^ rcon;
            t[1] = s_aes_sbox[t[2]];
            t[2] = s_aes_sbox[t[3]];
            t[3] = s_aes_sbox[u];
            rcon = (uint8_t)((rcon << 1) ^ ((rcon & 0x80) ? 0x1b : 0));
        }
        for (int j = 0; j < 4; j++) {
            ctx->rk[i * 4 + j] = ctx->rk[(i - 4) * 4 + j] ^ t[j];
        }
    }
}

static inline uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ ((x & 0x80u) ? 0x1bu : 0u)); }

static void aes128_encrypt_block(const uint8_t rk[176], const uint8_t in[16], uint8_t out[16])
{
    uint8_t s[16];
    for (int i = 0; i < 16; i++) s[i] = in[i] ^ rk[i];
    for (int r = 1; r <= 10; r++) {
        uint8_t t[16];
        for (int i = 0; i < 16; i++) t[i] = s_aes_sbox[s[i]];
        s[0]=t[0]; s[1]=t[5]; s[2]=t[10]; s[3]=t[15];
        s[4]=t[4]; s[5]=t[9]; s[6]=t[14]; s[7]=t[3];
        s[8]=t[8]; s[9]=t[13]; s[10]=t[2]; s[11]=t[7];
        s[12]=t[12]; s[13]=t[1]; s[14]=t[6]; s[15]=t[11];
        if (r < 10) {
            for (int c = 0; c < 4; c++) {
                uint8_t *col = &s[c * 4];
                uint8_t a0=col[0], a1=col[1], a2=col[2], a3=col[3];
                uint8_t tm = a0 ^ a1 ^ a2 ^ a3;
                col[0] ^= tm ^ xtime(a0 ^ a1);
                col[1] ^= tm ^ xtime(a1 ^ a2);
                col[2] ^= tm ^ xtime(a2 ^ a3);
                col[3] ^= tm ^ xtime(a3 ^ a0);
            }
        }
        for (int i = 0; i < 16; i++) s[i] ^= rk[r * 16 + i];
    }
    memcpy(out, s, 16);
}

/* RFC 4344 Section 4: AES-CTR increments 128-bit counter block as big-endian integer */
static void aes128_ctr_crypt(aes128_ctr_ctx_t *ctx, uint8_t *buf, size_t len)
{
    uint8_t ks[16];
    for (size_t off = 0; off < len; off += 16) {
        aes128_encrypt_block(ctx->rk, ctx->iv, ks);
        for (int i = 15; i >= 0; i--) {
            if (++ctx->iv[i] != 0) break;
        }
        size_t chunk = (len - off < 16) ? (len - off) : 16;
        for (size_t j = 0; j < chunk; j++) {
            buf[off + j] ^= ks[j];
        }
    }
}

/* PRNG seeded with microsecond timer + SHA-256 counter mode */
static uint32_t s_prng_counter = 1;
static void ssh_random_bytes(uint8_t *out, size_t len)
{
    size_t pos = 0;
    while (pos < len) {
        uint64_t us = hal_time_now_us();
        s_prng_counter += 0x9e3779b9u;
        sha256_ctx_t ctx;
        uint8_t digest[32];
        sha256_init(&ctx);
        sha256_update(&ctx, &us, sizeof(us));
        sha256_update(&ctx, &s_prng_counter, sizeof(s_prng_counter));
        sha256_final(&ctx, digest);
        size_t n = (len - pos < 32) ? (len - pos) : 32;
        memcpy(out + pos, digest, n);
        pos += n;
    }
}

/* =========================================================================
 * 2. SSH-2 Session Context & Buffer Helpers
 * ========================================================================= */

typedef struct {
    uint32_t         seq_c2s;
    uint32_t         seq_s2c;
    bool             encrypted;
    aes128_ctr_ctx_t enc_c2s;
    aes128_ctr_ctx_t dec_s2c;
    uint8_t          mac_key_c2s[32];
    uint8_t          mac_key_s2c[32];
    uint8_t          session_id[32];
    char             v_s[128];
    uint8_t          i_c[512];
    size_t           i_c_len;
    uint8_t          i_s[2048];
    size_t           i_s_len;
    uint8_t          tx_pkt[SSH_MAX_PKT];
    uint8_t          rx_pkt[SSH_MAX_PKT];
    uint8_t          payload[SSH_MAX_PKT - 64];
    uint32_t         remote_chan;
    uint32_t         remote_win;
    uint32_t         local_win_consumed;
    int              exit_status;
} ssh_session_t;

static void put_u32_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

static uint32_t get_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static size_t buf_put_u32(uint8_t *buf, size_t pos, uint32_t v)
{
    put_u32_be(buf + pos, v);
    return pos + 4;
}

static size_t buf_put_u8(uint8_t *buf, size_t pos, uint8_t v)
{
    buf[pos] = v;
    return pos + 1;
}

static size_t buf_put_mem(uint8_t *buf, size_t pos, const void *data, size_t len)
{
    put_u32_be(buf + pos, (uint32_t)len);
    if (len > 0) memcpy(buf + pos + 4, data, len);
    return pos + 4 + len;
}

static size_t buf_put_str(uint8_t *buf, size_t pos, const char *s)
{
    size_t len = s ? strlen(s) : 0;
    return buf_put_mem(buf, pos, s, len);
}

static bool buf_get_u32(const uint8_t *buf, size_t len, size_t *pos, uint32_t *out)
{
    if (*pos + 4 > len) return false;
    *out = get_u32_be(buf + *pos);
    *pos += 4;
    return true;
}

static bool buf_get_str(const uint8_t *buf, size_t len, size_t *pos, const uint8_t **out_ptr, size_t *out_len)
{
    uint32_t slen = 0;
    if (!buf_get_u32(buf, len, pos, &slen)) return false;
    if (*pos + slen > len) return false;
    *out_ptr = buf + *pos;
    *out_len = slen;
    *pos += slen;
    return true;
}

static void sha256_update_ssh_str(sha256_ctx_t *ctx, const void *data, size_t len)
{
    uint8_t lbe[4];
    put_u32_be(lbe, (uint32_t)len);
    sha256_update(ctx, lbe, 4);
    if (len > 0) sha256_update(ctx, data, len);
}

/* Read exact `len` bytes from TCP within `timeout_ms`, returning -1 on error, 0 on timeout before first byte */
static int tcp_read_exact(uint8_t *buf, size_t len, uint32_t first_timeout_ms, uint32_t cont_timeout_ms)
{
    size_t got = 0;
    while (got < len) {
        uint32_t tmo = (got == 0) ? first_timeout_ms : cont_timeout_ms;
        int r = wifi_tcp_recv(buf + got, len - got, tmo);
        if (r < 0) return -1;
        if (r == 0) {
            if (got == 0) return 0;
            return -1;
        }
        got += (size_t)r;
    }
    return (int)got;
}

static bool ssh_send_packet(ssh_session_t *s, const uint8_t *payload, size_t payload_len)
{
    size_t block_sz = s->encrypted ? 16u : 8u;
    size_t unpadded = 5u + payload_len;
    size_t pad_len  = block_sz - (unpadded & (block_sz - 1u));
    if (pad_len < 4u) pad_len += block_sz;

    size_t pkt_len = 1u + payload_len + pad_len;
    size_t total_len = 4u + pkt_len;
    if (total_len + 32u > SSH_MAX_PKT) return false;

    put_u32_be(s->tx_pkt, (uint32_t)pkt_len);
    s->tx_pkt[4] = (uint8_t)pad_len;
    memcpy(s->tx_pkt + 5, payload, payload_len);
    ssh_random_bytes(s->tx_pkt + 5 + payload_len, pad_len);

    if (s->encrypted) {
        uint8_t mac[32];
        hmac_sha256_packet(s->mac_key_c2s, s->seq_c2s, s->tx_pkt, total_len, mac);
        aes128_ctr_crypt(&s->enc_c2s, s->tx_pkt, total_len);
        memcpy(s->tx_pkt + total_len, mac, 32);
        total_len += 32u;
    }

    s->seq_c2s++;
    return wifi_tcp_send(s->tx_pkt, total_len, 10000) == (int)total_len;
}

/*
 * Receive one SSH-2 packet.
 * Returns:
 *   > 0 : payload length (payload stored at *out_payload = s->rx_pkt + 5)
 *     0 : timeout waiting for packet
 *   < 0 : connection closed or protocol/MAC error
 */
static int ssh_recv_packet(ssh_session_t *s, uint8_t **out_payload, uint32_t timeout_ms)
{
    for (;;) {
        if (!s->encrypted) {
            int r = tcp_read_exact(s->rx_pkt, 4, timeout_ms, 8000);
            if (r <= 0) return r;
            uint32_t pkt_len = get_u32_be(s->rx_pkt);
            if (pkt_len < 5u || 4u + pkt_len > SSH_MAX_PKT) {
                kprintf("ssh: bad unencrypted packet length %u\n", pkt_len);
                return -1;
            }
            if (tcp_read_exact(s->rx_pkt + 4, pkt_len, 8000, 8000) != (int)pkt_len) return -1;
            uint8_t pad_len = s->rx_pkt[4];
            if ((uint32_t)pad_len + 1u >= pkt_len) return -1;
            size_t payload_len = pkt_len - 1u - pad_len;
            s->seq_s2c++;
            uint8_t msg_type = s->rx_pkt[5];
            if (msg_type == SSH_MSG_IGNORE || msg_type == SSH_MSG_DEBUG ||
                msg_type == SSH_MSG_UNIMPLEMENTED || msg_type == SSH_MSG_EXT_INFO) {
                continue;
            }
            if (msg_type == SSH_MSG_DISCONNECT) {
                size_t pos = 1;
                uint32_t reason = 0;
                const uint8_t *desc = NULL;
                size_t dlen = 0;
                buf_get_u32(s->rx_pkt + 5, payload_len, &pos, &reason);
                if (buf_get_str(s->rx_pkt + 5, payload_len, &pos, &desc, &dlen) && dlen > 0) {
                    kprintf("\nssh: remote disconnected (%u): ", reason);
                    kconsole_write((const char *)desc, dlen);
                    kprintf("\n");
                }
                return -1;
            }
            *out_payload = s->rx_pkt + 5;
            return (int)payload_len;
        } else {
            int r = tcp_read_exact(s->rx_pkt, 16, timeout_ms, 8000);
            if (r <= 0) return r;
            aes128_ctr_crypt(&s->dec_s2c, s->rx_pkt, 16);
            uint32_t pkt_len = get_u32_be(s->rx_pkt);
            size_t total_len = 4u + pkt_len;
            if (pkt_len < 12u || total_len > SSH_MAX_PKT || (total_len & 15u) != 0) {
                kprintf("ssh: bad encrypted packet length %u\n", pkt_len);
                return -1;
            }
            if (total_len > 16u) {
                size_t rem = total_len - 16u;
                if (tcp_read_exact(s->rx_pkt + 16, rem, 8000, 8000) != (int)rem) return -1;
                aes128_ctr_crypt(&s->dec_s2c, s->rx_pkt + 16, rem);
            }
            uint8_t rx_mac[32], exp_mac[32];
            if (tcp_read_exact(rx_mac, 32, 8000, 8000) != 32) return -1;
            hmac_sha256_packet(s->mac_key_s2c, s->seq_s2c, s->rx_pkt, total_len, exp_mac);
            uint8_t diff = 0;
            for (int i = 0; i < 32; i++) diff |= (uint8_t)(rx_mac[i] ^ exp_mac[i]);
            if (diff != 0) {
                kprintf("ssh: MAC verification failed on seq %u\n", s->seq_s2c);
                return -1;
            }
            s->seq_s2c++;
            uint8_t pad_len = s->rx_pkt[4];
            if ((uint32_t)pad_len + 1u >= pkt_len) return -1;
            size_t payload_len = pkt_len - 1u - pad_len;
            uint8_t msg_type = s->rx_pkt[5];
            if (msg_type == SSH_MSG_IGNORE || msg_type == SSH_MSG_DEBUG ||
                msg_type == SSH_MSG_UNIMPLEMENTED || msg_type == SSH_MSG_EXT_INFO) {
                continue;
            }
            if (msg_type == SSH_MSG_DISCONNECT) {
                size_t pos = 1;
                uint32_t reason = 0;
                const uint8_t *desc = NULL;
                size_t dlen = 0;
                buf_get_u32(s->rx_pkt + 5, payload_len, &pos, &reason);
                if (buf_get_str(s->rx_pkt + 5, payload_len, &pos, &desc, &dlen) && dlen > 0) {
                    kprintf("\nssh: remote disconnected (%u): ", reason);
                    kconsole_write((const char *)desc, dlen);
                    kprintf("\n");
                }
                return -1;
            }
            *out_payload = s->rx_pkt + 5;
            return (int)payload_len;
        }
    }
}

/* =========================================================================
 * 3. Key Exchange (curve25519-sha256, RFC 8731) & Key Derivation
 * ========================================================================= */

static void ssh_derive_key(const uint8_t *k_mpint, size_t k_mpint_len,
                           const uint8_t h[32], const uint8_t session_id[32],
                           char letter, uint8_t out[32])
{
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, k_mpint, k_mpint_len);
    sha256_update(&ctx, h, 32);
    uint8_t ch = (uint8_t)letter;
    sha256_update(&ctx, &ch, 1);
    sha256_update(&ctx, session_id, 32);
    sha256_final(&ctx, out);
}

static bool ssh_handshake(ssh_session_t *s)
{
    /* 1. Read Server Identification String (RFC 4253 Section 4.2) */
    for (;;) {
        size_t len = 0;
        while (len < sizeof(s->v_s) - 1) {
            uint8_t c = 0;
            int r = tcp_read_exact(&c, 1, 8000, 8000);
            if (r <= 0) {
                kprintf("ssh: timeout waiting for server banner\n");
                return false;
            }
            if (c == '\n') break;
            if (c != '\r') s->v_s[len++] = (char)c;
        }
        s->v_s[len] = 0;
        if (len >= 4 && memcmp(s->v_s, "SSH-", 4) == 0) break;
    }

    /* 2. Send Client Identification String */
    const char *banner_wire = SSH_BANNER_CLIENT "\r\n";
    if (wifi_tcp_send(banner_wire, strlen(banner_wire), 5000) != (int)strlen(banner_wire)) {
        kprintf("ssh: failed to send client banner\n");
        return false;
    }

    /* 3. Build and send client SSH_MSG_KEXINIT (20) */
    size_t p = 0;
    p = buf_put_u8(s->i_c, p, SSH_MSG_KEXINIT);
    ssh_random_bytes(s->i_c + p, 16);
    p += 16;
    p = buf_put_str(s->i_c, p, "curve25519-sha256,curve25519-sha256@libssh.org");
    p = buf_put_str(s->i_c, p, "ssh-ed25519,ecdsa-sha2-nistp256,rsa-sha2-256,rsa-sha2-512,ssh-rsa");
    p = buf_put_str(s->i_c, p, "aes128-ctr");
    p = buf_put_str(s->i_c, p, "aes128-ctr");
    p = buf_put_str(s->i_c, p, "hmac-sha2-256");
    p = buf_put_str(s->i_c, p, "hmac-sha2-256");
    p = buf_put_str(s->i_c, p, "none");
    p = buf_put_str(s->i_c, p, "none");
    p = buf_put_str(s->i_c, p, "");
    p = buf_put_str(s->i_c, p, "");
    p = buf_put_u8(s->i_c, p, 0);     /* first_kex_packet_follows = FALSE */
    p = buf_put_u32(s->i_c, p, 0);    /* reserved = 0 */
    s->i_c_len = p;

    if (!ssh_send_packet(s, s->i_c, s->i_c_len)) return false;

    /* 4. Receive server SSH_MSG_KEXINIT (20) */
    uint8_t *rx = NULL;
    int rx_len = ssh_recv_packet(s, &rx, 10000);
    if (rx_len <= 0 || rx[0] != SSH_MSG_KEXINIT || (size_t)rx_len > sizeof(s->i_s)) {
        kprintf("ssh: expected KEXINIT from server\n");
        return false;
    }
    memcpy(s->i_s, rx, (size_t)rx_len);
    s->i_s_len = (size_t)rx_len;

    /* 5. Generate ephemeral X25519 keypair and send SSH_MSG_KEX_ECDH_INIT (30) */
    uint8_t priv_c[32], q_c[32];
    static const uint8_t basepoint[32] = { 9 };
    ssh_random_bytes(priv_c, 32);
    priv_c[0]  &= 248u;
    priv_c[31]  = (uint8_t)((priv_c[31] & 127u) | 64u);
    x25519_scalarmult(q_c, priv_c, basepoint);

    p = 0;
    p = buf_put_u8(s->payload, p, SSH_MSG_KEX_ECDH_INIT);
    p = buf_put_mem(s->payload, p, q_c, 32);
    if (!ssh_send_packet(s, s->payload, p)) return false;

    /* 6. Receive SSH_MSG_KEX_ECDH_REPLY (31) */
    rx_len = ssh_recv_packet(s, &rx, 10000);
    if (rx_len <= 0 || rx[0] != SSH_MSG_KEX_ECDH_REPLY) {
        kprintf("ssh: expected KEX_ECDH_REPLY (got %d)\n", rx_len > 0 ? rx[0] : rx_len);
        return false;
    }
    size_t pos = 1;
    const uint8_t *k_s = NULL, *q_s = NULL, *sig_h = NULL;
    size_t k_s_len = 0, q_s_len = 0, sig_h_len = 0;
    if (!buf_get_str(rx, (size_t)rx_len, &pos, &k_s, &k_s_len) ||
        !buf_get_str(rx, (size_t)rx_len, &pos, &q_s, &q_s_len) ||
        !buf_get_str(rx, (size_t)rx_len, &pos, &sig_h, &sig_h_len) ||
        q_s_len != 32) {
        kprintf("ssh: malformed KEX_ECDH_REPLY\n");
        return false;
    }

    /* Display Server Host Key Type & SHA-256 Fingerprint */
    uint8_t hk_fp[32];
    sha256_ctx_t hctx;
    sha256_init(&hctx);
    sha256_update(&hctx, k_s, k_s_len);
    sha256_final(&hctx, hk_fp);

    size_t hk_pos = 0;
    const uint8_t *hk_type = NULL;
    size_t hk_type_len = 0;
    char hk_type_str[32] = "hostkey";
    if (buf_get_str(k_s, k_s_len, &hk_pos, &hk_type, &hk_type_len) && hk_type_len < sizeof(hk_type_str)) {
        memcpy(hk_type_str, hk_type, hk_type_len);
        hk_type_str[hk_type_len] = 0;
    }
    kprintf("ssh: server %s (%s) SHA256:", s->v_s, hk_type_str);
    for (int i = 0; i < 8; i++) kprintf("%02x", hk_fp[i]);
    kprintf("...\n");

    /* 7. Compute shared secret K = X25519(priv_c, Q_S) and encode as SSH mpint */
    uint8_t k_raw[32];
    x25519_scalarmult(k_raw, priv_c, q_s);
    uint8_t nz = 0;
    for (int i = 0; i < 32; i++) nz |= k_raw[i];
    if (nz == 0) {
        kprintf("ssh: invalid X25519 public key\n");
        return false;
    }

    int first_nz = 0;
    while (first_nz < 32 && k_raw[first_nz] == 0) first_nz++;
    uint8_t k_mpint[37];
    size_t k_mpint_len = 0;
    if (first_nz == 32) {
        put_u32_be(k_mpint, 0);
        k_mpint_len = 4;
    } else {
        size_t rem = (size_t)(32 - first_nz);
        bool need_zero = (k_raw[first_nz] & 0x80u) != 0;
        put_u32_be(k_mpint, (uint32_t)(rem + (need_zero ? 1u : 0u)));
        size_t off = 4;
        if (need_zero) k_mpint[off++] = 0x00u;
        memcpy(k_mpint + off, k_raw + first_nz, rem);
        k_mpint_len = off + rem;
    }

    /* 8. Compute Exchange Hash H = SHA256(V_C || V_S || I_C || I_S || K_S || Q_C || Q_S || K) */
    uint8_t h[32];
    sha256_init(&hctx);
    sha256_update_ssh_str(&hctx, SSH_BANNER_CLIENT, strlen(SSH_BANNER_CLIENT));
    sha256_update_ssh_str(&hctx, s->v_s, strlen(s->v_s));
    sha256_update_ssh_str(&hctx, s->i_c, s->i_c_len);
    sha256_update_ssh_str(&hctx, s->i_s, s->i_s_len);
    sha256_update_ssh_str(&hctx, k_s, k_s_len);
    sha256_update_ssh_str(&hctx, q_c, 32);
    sha256_update_ssh_str(&hctx, q_s, 32);
    sha256_update(&hctx, k_mpint, k_mpint_len);
    sha256_final(&hctx, h);
    memcpy(s->session_id, h, 32);

    /* 9. Exchange SSH_MSG_NEWKEYS (21) */
    s->payload[0] = SSH_MSG_NEWKEYS;
    if (!ssh_send_packet(s, s->payload, 1)) return false;

    rx_len = ssh_recv_packet(s, &rx, 10000);
    if (rx_len <= 0 || rx[0] != SSH_MSG_NEWKEYS) {
        kprintf("ssh: expected NEWKEYS from server\n");
        return false;
    }

    /* 10. Derive directional IVs, AES-128 keys, and HMAC-SHA-256 keys */
    uint8_t iv_c2s[32], iv_s2c[32], key_c2s[32], key_s2c[32];
    ssh_derive_key(k_mpint, k_mpint_len, h, s->session_id, 'A', iv_c2s);
    ssh_derive_key(k_mpint, k_mpint_len, h, s->session_id, 'B', iv_s2c);
    ssh_derive_key(k_mpint, k_mpint_len, h, s->session_id, 'C', key_c2s);
    ssh_derive_key(k_mpint, k_mpint_len, h, s->session_id, 'D', key_s2c);
    ssh_derive_key(k_mpint, k_mpint_len, h, s->session_id, 'E', s->mac_key_c2s);
    ssh_derive_key(k_mpint, k_mpint_len, h, s->session_id, 'F', s->mac_key_s2c);

    aes128_init(&s->enc_c2s, key_c2s, iv_c2s);
    aes128_init(&s->dec_s2c, key_s2c, iv_s2c);
    s->encrypted = true;
    return true;
}

/* =========================================================================
 * 4. User Authentication (password & keyboard-interactive)
 * ========================================================================= */

static bool read_password_console(const char *prompt, char *out, size_t max_len)
{
    kprintf("%s", prompt);
    size_t len = 0;
    int prev = 0;
    for (;;) {
        int c = kconsole_getc();
        if (c < 0) {
            task_sleep(10);
            continue;
        }
        if (c == '\n' && prev == '\r') {
            prev = c;
            continue;
        }
        prev = c;
        if (c == '\r' || c == '\n') {
            kprintf("\n");
            out[len] = 0;
            return true;
        }
        if (c == 0x03) { /* Ctrl-C */
            kprintf("^C\n");
            out[0] = 0;
            return false;
        }
        if (c == 0x08 || c == 0x7F) {
            if (len > 0) len--;
            continue;
        }
        if (c >= 0x20 && c < 0x7F && len + 1 < max_len) {
            out[len++] = (char)c;
        }
    }
}

static bool ssh_authenticate(ssh_session_t *s, const char *user, const char *host, char *password, size_t pass_cap)
{
    /* Request ssh-userauth service */
    size_t p = 0;
    p = buf_put_u8(s->payload, p, SSH_MSG_SERVICE_REQUEST);
    p = buf_put_str(s->payload, p, "ssh-userauth");
    if (!ssh_send_packet(s, s->payload, p)) return false;

    uint8_t *rx = NULL;
    int rx_len = ssh_recv_packet(s, &rx, 10000);
    if (rx_len <= 0 || rx[0] != SSH_MSG_SERVICE_ACCEPT) {
        kprintf("ssh: ssh-userauth service rejected\n");
        return false;
    }

    /* Probe "none" auth first */
    p = 0;
    p = buf_put_u8(s->payload, p, SSH_MSG_USERAUTH_REQUEST);
    p = buf_put_str(s->payload, p, user);
    p = buf_put_str(s->payload, p, "ssh-connection");
    p = buf_put_str(s->payload, p, "none");
    if (!ssh_send_packet(s, s->payload, p)) return false;

    for (;;) {
        rx_len = ssh_recv_packet(s, &rx, 10000);
        if (rx_len <= 0) return false;
        if (rx[0] == SSH_MSG_USERAUTH_BANNER) {
            size_t pos = 1;
            const uint8_t *bmsg = NULL;
            size_t blen = 0;
            if (buf_get_str(rx, (size_t)rx_len, &pos, &bmsg, &blen) && blen > 0) {
                kconsole_write((const char *)bmsg, blen);
            }
            continue;
        }
        if (rx[0] == SSH_MSG_USERAUTH_SUCCESS) return true;
        if (rx[0] == SSH_MSG_USERAUTH_FAILURE) break;
        return false;
    }

    /* Prompt for password if none was provided yet */
    if (!password[0]) {
        char prompt[96];
        ksnprintf(prompt, sizeof(prompt), "%s@%s's password: ", user, host);
        if (!read_password_console(prompt, password, pass_cap)) return false;
    }

    /* Try "password" method */
    p = 0;
    p = buf_put_u8(s->payload, p, SSH_MSG_USERAUTH_REQUEST);
    p = buf_put_str(s->payload, p, user);
    p = buf_put_str(s->payload, p, "ssh-connection");
    p = buf_put_str(s->payload, p, "password");
    p = buf_put_u8(s->payload, p, 0);
    p = buf_put_str(s->payload, p, password);
    if (!ssh_send_packet(s, s->payload, p)) return false;

    for (;;) {
        rx_len = ssh_recv_packet(s, &rx, 10000);
        if (rx_len <= 0) return false;
        if (rx[0] == SSH_MSG_USERAUTH_BANNER) {
            size_t pos = 1;
            const uint8_t *bmsg = NULL;
            size_t blen = 0;
            if (buf_get_str(rx, (size_t)rx_len, &pos, &bmsg, &blen) && blen > 0) {
                kconsole_write((const char *)bmsg, blen);
            }
            continue;
        }
        if (rx[0] == SSH_MSG_USERAUTH_SUCCESS) return true;
        if (rx[0] == SSH_MSG_USERAUTH_FAILURE) break;
        return false;
    }

    /* Fallback: try "keyboard-interactive" (PAM on macOS/Linux) */
    p = 0;
    p = buf_put_u8(s->payload, p, SSH_MSG_USERAUTH_REQUEST);
    p = buf_put_str(s->payload, p, user);
    p = buf_put_str(s->payload, p, "ssh-connection");
    p = buf_put_str(s->payload, p, "keyboard-interactive");
    p = buf_put_str(s->payload, p, "");
    p = buf_put_str(s->payload, p, "");
    if (!ssh_send_packet(s, s->payload, p)) return false;

    for (;;) {
        rx_len = ssh_recv_packet(s, &rx, 10000);
        if (rx_len <= 0) return false;
        if (rx[0] == SSH_MSG_USERAUTH_BANNER) {
            size_t pos = 1;
            const uint8_t *bmsg = NULL;
            size_t blen = 0;
            if (buf_get_str(rx, (size_t)rx_len, &pos, &bmsg, &blen) && blen > 0) {
                kconsole_write((const char *)bmsg, blen);
            }
            continue;
        }
        if (rx[0] == SSH_MSG_USERAUTH_SUCCESS) return true;
        if (rx[0] == SSH_MSG_USERAUTH_INFO_REQUEST) {
            size_t pos = 1;
            const uint8_t *name = NULL, *inst = NULL, *lang = NULL;
            size_t nlen = 0, ilen = 0, llen = 0;
            uint32_t num_prompts = 0;
            if (!buf_get_str(rx, (size_t)rx_len, &pos, &name, &nlen) ||
                !buf_get_str(rx, (size_t)rx_len, &pos, &inst, &ilen) ||
                !buf_get_str(rx, (size_t)rx_len, &pos, &lang, &llen) ||
                !buf_get_u32(rx, (size_t)rx_len, &pos, &num_prompts)) {
                return false;
            }
            p = 0;
            p = buf_put_u8(s->payload, p, SSH_MSG_USERAUTH_INFO_RESPONSE);
            p = buf_put_u32(s->payload, p, num_prompts);
            for (uint32_t i = 0; i < num_prompts; i++) {
                p = buf_put_str(s->payload, p, password);
            }
            if (!ssh_send_packet(s, s->payload, p)) return false;
            continue;
        }
        if (rx[0] == SSH_MSG_USERAUTH_FAILURE) {
            kprintf("ssh: Permission denied (password/keyboard-interactive)\n");
            return false;
        }
        return false;
    }
}

/* =========================================================================
 * 5. Session Channel & Interactive Terminal / Command Loop
 * ========================================================================= */

static int ssh_session_loop(ssh_session_t *s, const char *remote_cmd)
{
    /* Open "session" channel (local channel 0) */
    size_t p = 0;
    p = buf_put_u8(s->payload, p, SSH_MSG_CHANNEL_OPEN);
    p = buf_put_str(s->payload, p, "session");
    p = buf_put_u32(s->payload, p, 0);       /* sender channel = 0 */
    p = buf_put_u32(s->payload, p, 65536u);  /* initial window size */
    p = buf_put_u32(s->payload, p, 2048u);   /* max packet size */
    if (!ssh_send_packet(s, s->payload, p)) return 1;

    uint8_t *rx = NULL;
    int rx_len = 0;
    for (;;) {
        rx_len = ssh_recv_packet(s, &rx, 10000);
        if (rx_len <= 0) return 1;
        if (rx[0] == SSH_MSG_GLOBAL_REQUEST) {
            /* e.g. OpenSSH hostkeys-00@openssh.com */
            size_t pos = 1;
            const uint8_t *rname = NULL;
            size_t rlen = 0;
            if (buf_get_str(rx, (size_t)rx_len, &pos, &rname, &rlen) && pos < (size_t)rx_len && rx[pos] != 0) {
                s->payload[0] = SSH_MSG_REQUEST_FAILURE;
                ssh_send_packet(s, s->payload, 1);
            }
            continue;
        }
        if (rx[0] == SSH_MSG_CHANNEL_OPEN_CONFIRMATION) {
            size_t pos = 1;
            uint32_t rcpt = 0, snd = 0, win = 0, maxpkt = 0;
            if (!buf_get_u32(rx, (size_t)rx_len, &pos, &rcpt) ||
                !buf_get_u32(rx, (size_t)rx_len, &pos, &snd) ||
                !buf_get_u32(rx, (size_t)rx_len, &pos, &win) ||
                !buf_get_u32(rx, (size_t)rx_len, &pos, &maxpkt)) {
                return 1;
            }
            s->remote_chan = snd;
            s->remote_win  = win;
            break;
        }
        if (rx[0] == SSH_MSG_CHANNEL_OPEN_FAILURE) {
            kprintf("ssh: channel open failed\n");
            return 1;
        }
    }

    if (remote_cmd && remote_cmd[0]) {
        /* Execute single remote command */
        p = 0;
        p = buf_put_u8(s->payload, p, SSH_MSG_CHANNEL_REQUEST);
        p = buf_put_u32(s->payload, p, s->remote_chan);
        p = buf_put_str(s->payload, p, "exec");
        p = buf_put_u8(s->payload, p, 1); /* want_reply = TRUE */
        p = buf_put_str(s->payload, p, remote_cmd);
        if (!ssh_send_packet(s, s->payload, p)) return 1;
    } else {
        /* Request PTY (vt100, 80x45) + interactive shell */
        p = 0;
        p = buf_put_u8(s->payload, p, SSH_MSG_CHANNEL_REQUEST);
        p = buf_put_u32(s->payload, p, s->remote_chan);
        p = buf_put_str(s->payload, p, "pty-req");
        p = buf_put_u8(s->payload, p, 0); /* want_reply = FALSE */
        p = buf_put_str(s->payload, p, "vt100");
        p = buf_put_u32(s->payload, p, 80);
        p = buf_put_u32(s->payload, p, 45);
        p = buf_put_u32(s->payload, p, 0);
        p = buf_put_u32(s->payload, p, 0);
        uint8_t modes[1] = { 0 }; /* TTY_OP_END */
        p = buf_put_mem(s->payload, p, modes, 1);
        if (!ssh_send_packet(s, s->payload, p)) return 1;

        p = 0;
        p = buf_put_u8(s->payload, p, SSH_MSG_CHANNEL_REQUEST);
        p = buf_put_u32(s->payload, p, s->remote_chan);
        p = buf_put_str(s->payload, p, "shell");
        p = buf_put_u8(s->payload, p, 1); /* want_reply = TRUE */
        if (!ssh_send_packet(s, s->payload, p)) return 1;
        kprintf("ssh: connected (press ~. after Enter to disconnect)\n");
    }

    bool close_sent = false;
    int tilde_state = 1; /* 1 = after newline, 2 = saw '~' after newline */

    while (wifi_tcp_is_connected()) {
        /* 1. Drain keyboard/serial input and send as SSH_MSG_CHANNEL_DATA */
        uint8_t inbuf[64];
        size_t inlen = 0;
        while (inlen < sizeof(inbuf)) {
            int c = kconsole_getc();
            if (c < 0) break;
            if (tilde_state == 1 && c == '~') {
                tilde_state = 2;
                continue;
            }
            if (tilde_state == 2) {
                if (c == '.') {
                    kprintf("\nssh: disconnected by ~.\n");
                    return 0;
                }
                inbuf[inlen++] = '~';
                tilde_state = 0;
            }
            if (c == '\r' || c == '\n') tilde_state = 1;
            else tilde_state = 0;
            inbuf[inlen++] = (uint8_t)c;
        }
        if (inlen > 0 && s->remote_win > 0) {
            p = 0;
            p = buf_put_u8(s->payload, p, SSH_MSG_CHANNEL_DATA);
            p = buf_put_u32(s->payload, p, s->remote_chan);
            p = buf_put_mem(s->payload, p, inbuf, inlen);
            if (!ssh_send_packet(s, s->payload, p)) break;
            if (s->remote_win >= inlen) s->remote_win -= (uint32_t)inlen;
            else s->remote_win = 0;
        }

        /* 2. Poll incoming SSH packet (20 ms timeout) */
        rx_len = ssh_recv_packet(s, &rx, 20);
        if (rx_len < 0) break;
        if (rx_len == 0) continue;

        uint8_t msg = rx[0];
        if (msg == SSH_MSG_CHANNEL_DATA || msg == SSH_MSG_CHANNEL_EXTENDED_DATA) {
            size_t pos = 1;
            uint32_t chan = 0, dtype = 0;
            const uint8_t *data = NULL;
            size_t dlen = 0;
            if (!buf_get_u32(rx, (size_t)rx_len, &pos, &chan)) continue;
            if (msg == SSH_MSG_CHANNEL_EXTENDED_DATA && !buf_get_u32(rx, (size_t)rx_len, &pos, &dtype)) continue;
            if (buf_get_str(rx, (size_t)rx_len, &pos, &data, &dlen) && dlen > 0) {
                kconsole_write((const char *)data, dlen);
                s->local_win_consumed += (uint32_t)dlen;
                if (s->local_win_consumed >= 16384u) {
                    p = 0;
                    p = buf_put_u8(s->payload, p, SSH_MSG_CHANNEL_WINDOW_ADJUST);
                    p = buf_put_u32(s->payload, p, s->remote_chan);
                    p = buf_put_u32(s->payload, p, s->local_win_consumed);
                    ssh_send_packet(s, s->payload, p);
                    s->local_win_consumed = 0;
                }
            }
        } else if (msg == SSH_MSG_CHANNEL_WINDOW_ADJUST) {
            size_t pos = 1;
            uint32_t chan = 0, add = 0;
            if (buf_get_u32(rx, (size_t)rx_len, &pos, &chan) &&
                buf_get_u32(rx, (size_t)rx_len, &pos, &add)) {
                s->remote_win += add;
            }
        } else if (msg == SSH_MSG_CHANNEL_REQUEST) {
            size_t pos = 1;
            uint32_t chan = 0;
            const uint8_t *rtype = NULL;
            size_t rlen = 0;
            if (buf_get_u32(rx, (size_t)rx_len, &pos, &chan) &&
                buf_get_str(rx, (size_t)rx_len, &pos, &rtype, &rlen) &&
                pos < (size_t)rx_len) {
                uint8_t want_reply = rx[pos++];
                if (rlen == 11 && memcmp(rtype, "exit-status", 11) == 0) {
                    uint32_t code = 0;
                    if (buf_get_u32(rx, (size_t)rx_len, &pos, &code)) {
                        s->exit_status = (int)code;
                    }
                } else if (want_reply) {
                    p = 0;
                    p = buf_put_u8(s->payload, p, SSH_MSG_CHANNEL_FAILURE);
                    p = buf_put_u32(s->payload, p, s->remote_chan);
                    ssh_send_packet(s, s->payload, p);
                }
            }
        } else if (msg == SSH_MSG_GLOBAL_REQUEST) {
            size_t pos = 1;
            const uint8_t *rname = NULL;
            size_t rlen = 0;
            if (buf_get_str(rx, (size_t)rx_len, &pos, &rname, &rlen) && pos < (size_t)rx_len && rx[pos] != 0) {
                s->payload[0] = SSH_MSG_REQUEST_FAILURE;
                ssh_send_packet(s, s->payload, 1);
            }
        } else if (msg == SSH_MSG_CHANNEL_EOF || msg == SSH_MSG_CHANNEL_CLOSE) {
            if (!close_sent) {
                p = 0;
                p = buf_put_u8(s->payload, p, SSH_MSG_CHANNEL_CLOSE);
                p = buf_put_u32(s->payload, p, s->remote_chan);
                ssh_send_packet(s, s->payload, p);
                close_sent = true;
            }
            if (msg == SSH_MSG_CHANNEL_CLOSE) break;
        }
    }

    return s->exit_status;
}

/* =========================================================================
 * 6. Configuration Persistence (/home/ssh.cfg) & CLI / TUI Entry Points
 * ========================================================================= */

static void ssh_load_last_target(char *buf, size_t cap)
{
    buf[0] = 0;
    int fd = vfs_open(SSH_CFG_PATH, VFS_O_READ);
    if (fd < 0) return;
    int n = vfs_read(fd, buf, cap - 1);
    vfs_close(fd);
    if (n <= 0) { buf[0] = 0; return; }
    buf[n] = 0;
    for (int i = 0; i < n; i++) {
        if (buf[i] == '\r' || buf[i] == '\n') { buf[i] = 0; break; }
    }
}

static void ssh_save_last_target(const char *target)
{
    if (!target || !target[0]) return;
    int fd = vfs_open(SSH_CFG_PATH, VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
    if (fd < 0) return;
    vfs_write(fd, target, strlen(target));
    vfs_write(fd, "\n", 1);
    vfs_close(fd);
}

static void parse_user_host_port(const char *spec, char *user, size_t ucap,
                                 char *host, size_t hcap, uint16_t *port)
{
    const char *at = strchr(spec, '@');
    const char *hstart = spec;
    if (at) {
        size_t ulen = (size_t)(at - spec);
        if (ulen >= ucap) ulen = ucap - 1;
        memcpy(user, spec, ulen);
        user[ulen] = 0;
        hstart = at + 1;
    } else if (!user[0]) {
        strncpy(user, "root", ucap - 1);
        user[ucap - 1] = 0;
    }

    const char *colon = strchr(hstart, ':');
    if (colon) {
        size_t hlen = (size_t)(colon - hstart);
        if (hlen >= hcap) hlen = hcap - 1;
        memcpy(host, hstart, hlen);
        host[hlen] = 0;
        uint32_t p = 0;
        for (const char *s = colon + 1; *s >= '0' && *s <= '9'; s++) {
            p = p * 10u + (uint32_t)(*s - '0');
        }
        if (p > 0 && p <= 65535u) *port = (uint16_t)p;
    } else {
        strncpy(host, hstart, hcap - 1);
        host[hcap - 1] = 0;
    }
}

static int ssh_connect_and_run(const char *user, const char *host, uint16_t port,
                               char *password, size_t pass_cap, const char *remote_cmd)
{
    wifi_status_t st;
    wifi_get_status(&st);

    if (st.state != WIFI_STATE_CONNECTED) {
        char saved_ssid[WIFI_SSID_MAX + 1] = "";
        char saved_pass[WIFI_PASS_MAX + 1] = "";
        bool auto_conn = false;
        if (wifi_load_saved(saved_ssid, saved_pass, &auto_conn) && auto_conn && saved_ssid[0]) {
            kprintf("ssh: connecting to saved Wi-Fi '%s'...\n", saved_ssid);
            wifi_connect(saved_ssid, saved_pass, false);
            wifi_get_status(&st);
        }
    }

    if (st.state == WIFI_STATE_CONNECTED && st.ip[0] == 0) {
        kprintf("ssh: waiting for DHCP IPv4 on '%s'...\n", st.ssid);
        for (int i = 0; i < 60 && st.ip[0] == 0; i++) {
            task_sleep(100);
            wifi_get_status(&st);
        }
    }

    if (st.state != WIFI_STATE_CONNECTED) {
        kprintf("ssh: Wi-Fi is not connected (run 'wifi' first)\n");
        return 1;
    }
    if (st.ip[0] == 0) {
        kprintf("ssh: Wi-Fi is associated with '%s', but DHCP IPv4 address is not assigned yet\n", st.ssid);
        return 1;
    }

    uint8_t ip[4] = {0};
    if (!wifi_resolve_host(host, ip)) {
        kprintf("ssh: could not resolve hostname '%s'\n", host);
        return 1;
    }

    kprintf("ssh: connecting to %s@%s (%u.%u.%u.%u:%u)...\n",
            user, host, ip[0], ip[1], ip[2], ip[3], port);

    if (!wifi_tcp_connect(ip, port, 8000)) {
        kprintf("ssh: TCP connection to %u.%u.%u.%u:%u failed\n",
                ip[0], ip[1], ip[2], ip[3], port);
        return 1;
    }

    ssh_session_t *s = (ssh_session_t *)kcalloc(1, sizeof(ssh_session_t));
    if (!s) {
        wifi_tcp_close();
        kprintf("ssh: out of memory\n");
        return 1;
    }

    int rc = 1;
    if (ssh_handshake(s)) {
        if (ssh_authenticate(s, user, host, password, pass_cap)) {
            rc = ssh_session_loop(s, remote_cmd);
        }
    }

    /* Zero out sensitive password & key material */
    memset(password, 0, pass_cap);
    memset(s, 0, sizeof(*s));
    kfree(s);
    wifi_tcp_close();
    return rc;
}

void ssh_app_run(void)
{
    char target[96] = "";
    ssh_load_last_target(target, sizeof(target));
    if (!target[0]) {
        strcpy(target, "user@192.168.1.1");
    }

    dq_clear();
    if (!dq_input("SSH Target (user@host[:port])", target, sizeof(target)) || !target[0]) {
        return;
    }
    ssh_save_last_target(target);

    char pass[64] = "";
    dq_clear();
    if (!dq_input("SSH Password (leave empty to prompt)", pass, sizeof(pass))) {
        return;
    }

    char user[48] = "";
    char host[64] = "";
    uint16_t port = 22;
    parse_user_host_port(target, user, sizeof(user), host, sizeof(host), &port);

    /* Switch back to framebuffer console for live 80x45 terminal output */
    dq_clear();
    fbcon_pause(false);
    ssh_connect_and_run(user, host, port, pass, sizeof(pass), NULL);
}

int ssh_cmd_main(int argc, char **argv)
{
    if (argc < 2) {
        fbcon_pause(true);
        task_sleep(100);
        ssh_app_run();
        fbcon_pause(false);
        return 0;
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "help") == 0) {
        kprintf("usage: ssh [user@]host[:port] [-p port] [-l user] [-pw password] [command...]\n"
                "       ssh                    (open interactive TUI connection dialog)\n");
        return 0;
    }

    char user[48] = "";
    char host[64] = "";
    char pass[64] = "";
    char cmd[256] = "";
    uint16_t port = 22;

    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            uint32_t p = 0;
            for (const char *s = argv[i + 1]; *s >= '0' && *s <= '9'; s++) p = p * 10u + (uint32_t)(*s - '0');
            if (p > 0 && p <= 65535u) port = (uint16_t)p;
            i += 2;
        } else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
            strncpy(user, argv[i + 1], sizeof(user) - 1);
            i += 2;
        } else if ((strcmp(argv[i], "-pw") == 0 || strcmp(argv[i], "--pass") == 0) && i + 1 < argc) {
            strncpy(pass, argv[i + 1], sizeof(pass) - 1);
            i += 2;
        } else if (!host[0] && argv[i][0] != '-') {
            parse_user_host_port(argv[i], user, sizeof(user), host, sizeof(host), &port);
            i++;
        } else {
            /* Remaining arguments form the remote command */
            size_t clen = strlen(cmd);
            if (clen > 0 && clen + 1 < sizeof(cmd)) {
                cmd[clen++] = ' ';
                cmd[clen] = 0;
            }
            strncat(cmd, argv[i], sizeof(cmd) - clen - 1);
            i++;
        }
    }

    if (!host[0]) {
        kprintf("usage: ssh [user@]host[:port] [-p port] [-l user] [-pw password] [command...]\n");
        return 1;
    }

    char saved_spec[96];
    if (port != 22) ksnprintf(saved_spec, sizeof(saved_spec), "%s@%s:%u", user, host, port);
    else            ksnprintf(saved_spec, sizeof(saved_spec), "%s@%s", user, host);
    ssh_save_last_target(saved_spec);

    return ssh_connect_and_run(user, host, port, pass, sizeof(pass), cmd[0] ? cmd : NULL);
}

void ssh_main(void *arg)
{
    (void)arg;
    fbcon_pause(true);
    task_sleep(100);
    ssh_app_run();
    fbcon_pause(false);
}

