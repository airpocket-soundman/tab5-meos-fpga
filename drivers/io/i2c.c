/* Shared external SoftI2C. Never changes the internal board I2C pin selection. */
#include <m5os/io_i2c.h>
#include <m5os/hal_gpio.h>
#include <m5os/hal_time.h>
#include <m5os/mutex.h>
#include <m5os/arch.h>

#define I2C_SLOTS 4u
typedef struct {
    io_owner_t owner;
    uint32_t handle, half_us, stretch_us, timeout_us;
    int sda, scl;
    volatile bool running, cancelled;
    uint64_t start_us;
} soft_i2c_t;
static soft_i2c_t buses[I2C_SLOTS];
static uint32_t serial;
static mutex_t lock = MUTEX_INIT;

static soft_i2c_t *lookup(io_owner_t owner, uint32_t handle) {
    unsigned slot = handle & 255u;
    if (!slot || slot > I2C_SLOTS) return NULL;
    soft_i2c_t *b = &buses[slot - 1];
    return b->owner == owner && b->handle == handle ? b : NULL;
}
static int check(soft_i2c_t *b) {
    if (b->cancelled) return IO_ERR_CANCELLED;
    return hal_time_now_us() - b->start_us >= b->timeout_us ? ERR_TIMEOUT : 0;
}
static int delay(soft_i2c_t *b) {
    hal_delay_us(b->half_us);
    return check(b);
}
static int high(soft_i2c_t *b) {
    hal_gpio_od_release(b->scl);
    uint64_t start = hal_time_now_us();
    while (!hal_gpio_read(b->scl)) {
        int e = check(b);
        if (e) return e;
        if (hal_time_now_us() - start >= b->stretch_us) return ERR_TIMEOUT;
        hal_delay_us(1);
    }
    return check(b);
}
static int start(soft_i2c_t *b) {
    hal_gpio_od_release(b->sda);
    int e = delay(b);
    if (!e) e = high(b);
    if (!e) e = delay(b);
    if (!e && !hal_gpio_read(b->sda)) e = ERR_BUSY;
    if (e) return e;
    hal_gpio_od_drive_low(b->sda);
    e = delay(b);
    hal_gpio_od_drive_low(b->scl);
    return e;
}
/* Cleanup ignores cancellation, but never waits indefinitely for a held clock.
 * A slave holding SCL low prevents a valid STOP; release both lines anyway. */
static void stop(soft_i2c_t *b) {
    hal_gpio_od_drive_low(b->scl);
    hal_gpio_od_drive_low(b->sda);
    hal_delay_us(b->half_us);
    hal_gpio_od_release(b->scl);
    uint64_t begin = hal_time_now_us();
    while (!hal_gpio_read(b->scl) && hal_time_now_us() - begin < b->stretch_us)
        hal_delay_us(1);
    hal_delay_us(b->half_us);
    hal_gpio_od_release(b->sda);
    hal_delay_us(b->half_us);
}
static int write_byte(soft_i2c_t *b, uint8_t value) {
    for (unsigned i = 0; i < 8; i++) {
        if (value & 0x80) hal_gpio_od_release(b->sda);
        else hal_gpio_od_drive_low(b->sda);
        int e = delay(b);
        if (!e) e = high(b);
        if (!e) e = delay(b);
        hal_gpio_od_drive_low(b->scl);
        if (e) return e;
        value <<= 1;
    }
    hal_gpio_od_release(b->sda);
    int e = delay(b);
    if (!e) e = high(b);
    if (!e) e = delay(b);
    bool nack = hal_gpio_read(b->sda) != 0;
    hal_gpio_od_drive_low(b->scl);
    return e ? e : nack ? IO_ERR_NACK : 0;
}
static int read_byte(soft_i2c_t *b, uint8_t *out, bool ack) {
    uint8_t value = 0;
    hal_gpio_od_release(b->sda);
    for (unsigned i = 0; i < 8; i++) {
        int e = delay(b);
        if (!e) e = high(b);
        if (!e) e = delay(b);
        if (e) { hal_gpio_od_drive_low(b->scl); return e; }
        value = (uint8_t)((value << 1) | !!hal_gpio_read(b->sda));
        hal_gpio_od_drive_low(b->scl);
    }
    if (ack) hal_gpio_od_drive_low(b->sda);
    else hal_gpio_od_release(b->sda);
    int e = delay(b);
    if (!e) e = high(b);
    if (!e) e = delay(b);
    hal_gpio_od_drive_low(b->scl);
    hal_gpio_od_release(b->sda);
    if (!e) *out = value;
    return e;
}
int io_i2c_open(io_owner_t owner, const io_i2c_config_t *c, uint32_t *handle) {
    if (!c || !handle || c->sda == c->scl || c->frequency_hz < 1000 ||
        c->frequency_hz > 400000 || c->stretch_timeout_us > 100000 ||
        c->transfer_timeout_us > 10000000) return ERR_INVAL;
    if (!io_owner_valid(owner)) return ERR_PERM;
    mutex_lock(&lock);
    int e = ERR_BUSY;
    for (unsigned i = 0; i < I2C_SLOTS && serial < 0xffffffu; i++) {
        if (buses[i].owner) continue;
        int pins[2] = {c->sda, c->scl};
        e = io_pins_claim(owner, pins, 2, IO_USE_I2C);
        if (e) break;
        soft_i2c_t *b = &buses[i];
        *b = (soft_i2c_t){.owner = owner, .handle = (++serial << 8) | (i + 1),
            .sda = c->sda, .scl = c->scl,
            .half_us = (500000u + c->frequency_hz - 1) / c->frequency_hz,
            .stretch_us = c->stretch_timeout_us ? c->stretch_timeout_us : 10000,
            .timeout_us = c->transfer_timeout_us ? c->transfer_timeout_us : 2000000};
        hal_gpio_config_open_drain(b->sda);
        hal_gpio_config_open_drain(b->scl);
        hal_gpio_od_release(b->sda);
        hal_gpio_od_release(b->scl);
        *handle = b->handle;
        e = 0;
        break;
    }
    mutex_unlock(&lock);
    return e;
}
int io_i2c_transfer(io_owner_t owner, uint32_t handle, uint8_t address,
                    const uint8_t *tx, size_t nt, uint8_t *rx, size_t nr) {
    if (address > 0x7f || nt > 4096 || nr > 4096 || (nt && !tx) || (nr && !rx))
        return ERR_INVAL;
    if (!io_owner_valid(owner)) return ERR_PERM;
    mutex_lock(&lock);
    soft_i2c_t *b = io_owner_valid(owner) ? lookup(owner, handle) : NULL;
    int e = b ? 0 : ERR_BADF;
    if (b) {
        if (b->running) stop(b); /* Reclaimed mutex after a terminated caller. */
        uint32_t irq = arch_irq_disable();
        b->cancelled = false;
        b->running = true;
        arch_irq_restore(irq);
        /* Closing may have started after our earlier validity check. Clear
         * the previous transaction's cancel flag before this final check, so
         * owner cleanup cannot have its cancellation erased afterward. */
        if (!io_owner_valid(owner)) {
            b->running = false;
            mutex_unlock(&lock);
            return ERR_PERM;
        }
        b->start_us = hal_time_now_us();
        e = start(b);
        bool write = nt || !nr;
        if (!e) e = write_byte(b, (uint8_t)((address << 1) | !write));
        for (size_t i = 0; !e && i < nt; i++) e = write_byte(b, tx[i]);
        if (!e && nt && nr) {
            e = start(b);
            if (!e) e = write_byte(b, (uint8_t)((address << 1) | 1));
        }
        for (size_t i = 0; !e && i < nr; i++) e = read_byte(b, rx + i, i + 1 < nr);
        stop(b);
        b->running = false;
    }
    mutex_unlock(&lock);
    return e;
}
int io_i2c_cancel(io_owner_t owner, uint32_t handle) {
    if (!io_owner_valid(owner)) return ERR_PERM;
    uint32_t irq = arch_irq_disable();
    soft_i2c_t *b = lookup(owner, handle);
    int e = !b ? ERR_BADF : !b->running ? ERR_BUSY : 0;
    if (!e) b->cancelled = true;
    arch_irq_restore(irq);
    return e;
}
static void close_bus(soft_i2c_t *b) {
    /* Tombstone before releasing any pin. A task killed after registry release
     * must not leave a slot that later cleanup can use against a new owner. */
    soft_i2c_t old = *b;
    uint32_t irq = arch_irq_disable();
    b->owner = b->handle = 0;
    b->running = false;
    arch_irq_restore(irq);
    if (old.running) stop(&old);
    int pins[2] = {old.sda, old.scl};
    hal_gpio_od_release(old.sda);
    hal_gpio_od_release(old.scl);
    io_pins_release(old.owner, pins, 2);
}

int io_i2c_close(io_owner_t owner, uint32_t handle) {
    if (!io_owner_valid(owner)) return ERR_PERM;
    io_i2c_cancel(owner, handle);
    mutex_lock(&lock);
    soft_i2c_t *b = io_owner_valid(owner) ? lookup(owner, handle) : NULL;
    int e = b ? 0 : ERR_BADF;
    if (b) close_bus(b);
    mutex_unlock(&lock);
    return e;
}
void io_i2c_release_owner(io_owner_t owner) {
    uint32_t irq = arch_irq_disable();
    for (unsigned i = 0; i < I2C_SLOTS; i++)
        if (buses[i].owner == owner) buses[i].cancelled = true;
    arch_irq_restore(irq);
    mutex_lock(&lock);
    for (unsigned i = 0; i < I2C_SLOTS; i++)
        if (buses[i].owner == owner && owner) close_bus(&buses[i]);
    mutex_unlock(&lock);
}
