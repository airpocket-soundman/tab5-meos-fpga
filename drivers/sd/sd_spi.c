/*
 * SD card driver, SPI mode (SD Physical Layer Simplified Spec, "SPI Mode").
 *
 * Init sequence: >=74 clocks with CS high; CMD0 -> idle (R1=0x01); CMD8 to
 * tell v2 cards (and check the 2.7-3.6 V range); ACMD41 (CMD55+CMD41) with the
 * HCS bit until the card leaves idle; CMD58 reads OCR whose CCS bit says
 * whether addresses are in blocks (SDHC/SDXC) or bytes (SDSC); CMD9 reads the
 * CSD to get the capacity.  After init the clock goes to full speed.
 * CRC is off in SPI mode except for CMD0/CMD8, whose CRCs are fixed constants.
 */
#include "sd_spi.h"
#include <m5os/hal_sdspi.h>
#include <m5os/hal_time.h>
#include <m5os/bus.h>
#include <m5os/errno.h>
#include <m5os/kprintf.h>

#define CMD0    0
#define CMD8    8
#define CMD9    9
#define CMD10   10
#define CMD16   16
#define CMD17   17
#define CMD24   24
#define CMD55   55
#define CMD58   58
#define ACMD41  41

#define R1_IDLE            0x01
#define R1_ILLEGAL_CMD     0x04
#define R1_NO_RESPONSE     0xFF
#define TOKEN_START_BLOCK  0xFE
#define DATA_RESP_MASK     0x1F
#define DATA_RESP_ACCEPTED 0x05
#define CRC_CMD0           0x95
#define CRC_CMD8           0x87
#define CRC_DUMMY          0x01
#define ARG_CMD8_CHECK     0x000001AAu
#define ARG_ACMD41_HCS     0x40000000u
#define OCR_CCS            0x40000000u
#define BLOCK_SIZE         512u
#define INIT_CLOCKS_BYTES  10                /* 80 clocks */
#define R1_POLL_BYTES      10
#define INIT_TIMEOUT_MS    1500u
#define BUSY_TIMEOUT_MS    500u
#define READ_TOKEN_TIMEOUT_MS 200u

static struct {
    bool     ready;
    bool     sdhc;            /* block addressing */
    uint32_t sectors;
    uint8_t  cid[16];
    uint8_t  csd[16];
} sd;

static blockdev_t s_bd;

static uint32_t now_ms(void) { return (uint32_t)(hal_time_now_us() / 1000u); }

static void clocks(int nbytes) { while (nbytes--) hal_sdspi_xfer(0xFF); }

static bool wait_ready(uint32_t timeout_ms)       /* card holds MISO low while busy */
{
    uint32_t t0 = now_ms();
    while (hal_sdspi_xfer(0xFF) != 0xFF) {
        if (now_ms() - t0 > timeout_ms) return false;
    }
    return true;
}

/* Send a command frame, return R1 (0xFF = no answer).  CS must be selected. */
static uint8_t send_cmd(uint8_t cmd, uint32_t arg, uint8_t crc)
{
    if (cmd != CMD0) wait_ready(BUSY_TIMEOUT_MS);
    hal_sdspi_xfer(0x40 | cmd);
    hal_sdspi_xfer((uint8_t)(arg >> 24));
    hal_sdspi_xfer((uint8_t)(arg >> 16));
    hal_sdspi_xfer((uint8_t)(arg >> 8));
    hal_sdspi_xfer((uint8_t)arg);
    hal_sdspi_xfer(crc);
    uint8_t r = R1_NO_RESPONSE;
    for (int i = 0; i < R1_POLL_BYTES; i++) {
        r = hal_sdspi_xfer(0xFF);
        if (!(r & 0x80)) break;
    }
    return r;
}

static void deselect(void)
{
    hal_sdspi_cs(false);
    hal_sdspi_xfer(0xFF);                         /* one extra clock releases MISO */
}

static bool read_data_block(uint8_t *buf, size_t len)   /* token, data, CRC16 (ignored) */
{
    uint32_t t0 = now_ms();
    uint8_t tok;
    while ((tok = hal_sdspi_xfer(0xFF)) == 0xFF) {
        if (now_ms() - t0 > READ_TOKEN_TIMEOUT_MS) return false;
    }
    if (tok != TOKEN_START_BLOCK) return false;
    for (size_t i = 0; i < len; i++) buf[i] = hal_sdspi_xfer(0xFF);
    hal_sdspi_xfer(0xFF);
    hal_sdspi_xfer(0xFF);
    return true;
}

static uint32_t csd_sectors(const uint8_t *c)
{
    if ((c[0] >> 6) == 1) {                      /* CSD v2.0: (C_SIZE + 1) * 512 KiB */
        uint32_t c_size = ((uint32_t)(c[7] & 0x3F) << 16) | ((uint32_t)c[8] << 8) | c[9];
        return (c_size + 1u) * 1024u;
    }
    uint32_t read_bl_len = c[5] & 0x0F;           /* CSD v1.0 */
    uint32_t c_size = ((uint32_t)(c[6] & 3) << 10) | ((uint32_t)c[7] << 2) | (c[8] >> 6);
    uint32_t mult = ((c[9] & 3u) << 1) | (c[10] >> 7);
    uint32_t bytes_shift = read_bl_len + mult + 2;
    return (uint32_t)(((uint64_t)(c_size + 1) << bytes_shift) / BLOCK_SIZE);
}

static int sd_read_blocks(blockdev_t *bd, uint32_t lba, uint32_t count, void *buf)
{
    (void)bd;
    if (!sd.ready) return ERR_NODEV;
    if (lba + count > sd.sectors) return ERR_INVAL;
    uint8_t *p = buf;
    int rc = ERR_OK;
    spi_bus_lock();
    hal_sdspi_begin();
    hal_sdspi_cs(true);
    for (uint32_t i = 0; i < count && rc == ERR_OK; i++) {
        uint32_t addr = sd.sdhc ? lba + i : (lba + i) * BLOCK_SIZE;
        if (send_cmd(CMD17, addr, CRC_DUMMY) != 0 || !read_data_block(p + i * BLOCK_SIZE, BLOCK_SIZE)) {
            rc = ERR_IO;
        }
    }
    deselect();
    hal_sdspi_end();
    spi_bus_unlock();
    return rc;
}

static int sd_write_blocks(blockdev_t *bd, uint32_t lba, uint32_t count, const void *buf)
{
    (void)bd;
    if (!sd.ready) return ERR_NODEV;
    if (lba + count > sd.sectors) return ERR_INVAL;
    const uint8_t *p = buf;
    int rc = ERR_OK;
    spi_bus_lock();
    hal_sdspi_begin();
    hal_sdspi_cs(true);
    for (uint32_t i = 0; i < count && rc == ERR_OK; i++) {
        uint32_t addr = sd.sdhc ? lba + i : (lba + i) * BLOCK_SIZE;
        if (send_cmd(CMD24, addr, CRC_DUMMY) != 0) { rc = ERR_IO; break; }
        hal_sdspi_xfer(0xFF);
        hal_sdspi_xfer(TOKEN_START_BLOCK);
        for (uint32_t k = 0; k < BLOCK_SIZE; k++) hal_sdspi_xfer(p[i * BLOCK_SIZE + k]);
        hal_sdspi_xfer(0xFF);                     /* CRC16 (not checked in SPI mode) */
        hal_sdspi_xfer(0xFF);
        if ((hal_sdspi_xfer(0xFF) & DATA_RESP_MASK) != DATA_RESP_ACCEPTED) { rc = ERR_IO; break; }
        if (!wait_ready(BUSY_TIMEOUT_MS)) rc = ERR_IO;     /* programming finished? */
    }
    deselect();
    hal_sdspi_end();
    spi_bus_unlock();
    return rc;
}

blockdev_t *sd_init(void)
{
    spi_bus_lock();
    hal_sdspi_begin();
    hal_sdspi_set_slow(true);
    hal_sdspi_cs(false);
    clocks(INIT_CLOCKS_BYTES);

    bool ok = false;
    hal_sdspi_cs(true);
    uint8_t r = R1_NO_RESPONSE;
    for (int tries = 0; tries < 5 && r != R1_IDLE; tries++) r = send_cmd(CMD0, 0, CRC_CMD0);
    if (r != R1_IDLE) {
        kprintf("sd: no card (CMD0 -> 0x%02x)\n", r);
        goto out;
    }

    bool v2 = false;
    r = send_cmd(CMD8, ARG_CMD8_CHECK, CRC_CMD8);
    if (r == R1_IDLE) {
        uint8_t r7[4];
        for (int i = 0; i < 4; i++) r7[i] = hal_sdspi_xfer(0xFF);
        if (r7[2] != 0x01 || r7[3] != 0xAA) { kprintf("sd: CMD8 echo mismatch\n"); goto out; }
        v2 = true;
    } else if (!(r & R1_ILLEGAL_CMD)) {
        kprintf("sd: CMD8 -> 0x%02x\n", r);
        goto out;
    }

    uint32_t t0 = now_ms();
    do {
        send_cmd(CMD55, 0, CRC_DUMMY);
        r = send_cmd(ACMD41, v2 ? ARG_ACMD41_HCS : 0, CRC_DUMMY);
    } while (r != 0 && now_ms() - t0 < INIT_TIMEOUT_MS);
    if (r != 0) { kprintf("sd: ACMD41 timeout (0x%02x)\n", r); goto out; }

    sd.sdhc = false;
    if (v2) {
        if (send_cmd(CMD58, 0, CRC_DUMMY) == 0) {
            uint32_t ocr = 0;
            for (int i = 0; i < 4; i++) ocr = (ocr << 8) | hal_sdspi_xfer(0xFF);
            sd.sdhc = (ocr & OCR_CCS) != 0;
        }
    }
    if (!sd.sdhc) send_cmd(CMD16, BLOCK_SIZE, CRC_DUMMY);

    hal_sdspi_set_slow(false);                    /* init done: full speed */

    if (send_cmd(CMD9, 0, CRC_DUMMY) != 0 || !read_data_block(sd.csd, sizeof sd.csd)) {
        kprintf("sd: cannot read CSD\n"); goto out;
    }
    deselect(); hal_sdspi_cs(true);
    if (send_cmd(CMD10, 0, CRC_DUMMY) != 0 || !read_data_block(sd.cid, sizeof sd.cid)) {
        kprintf("sd: cannot read CID\n"); goto out;
    }
    sd.sectors = csd_sectors(sd.csd);
    sd.ready = true;
    ok = true;

out:
    deselect();
    hal_sdspi_end();
    spi_bus_unlock();
    if (!ok) return NULL;

    s_bd = (blockdev_t){ .name = "sd0", .block_size = BLOCK_SIZE, .block_count = sd.sectors,
                         .read = sd_read_blocks, .write = sd_write_blocks };
    return &s_bd;
}

void sd_print_info(void)
{
    if (!sd.ready) { kprintf("sd: not ready\n"); return; }
    const uint8_t *c = sd.cid;
    char name[6] = { (char)c[3], (char)c[4], (char)c[5], (char)c[6], (char)c[7], 0 };
    kprintf("sd: %s, %u MiB (%u sectors), product \"%s\", rev %u.%u\n",
            sd.sdhc ? "SDHC/SDXC" : "SDSC", sd.sectors / 2048u, sd.sectors, name, c[8] >> 4, c[8] & 0x0F);
}
