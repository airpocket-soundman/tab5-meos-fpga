/* Shared external SoftSPI: single selected device per transaction, no RTOS. */
#include <m5os/io_spi.h>
#include <m5os/hal_gpio.h>
#include <m5os/hal_io_gpio.h>
#include <m5os/hal_time.h>
#include <m5os/mutex.h>
#include <m5os/arch.h>

#define SPI_SLOTS 4u
typedef struct {
    io_owner_t owner;
    uint32_t handle, half_us, timeout_us;
    int sck, mosi, miso, cs;
    uint8_t mode;
    volatile bool running, cancelled;
    uint64_t start_us;
} soft_spi_t;
static soft_spi_t buses[SPI_SLOTS];
static uint32_t serial;
static mutex_t lock = MUTEX_INIT;
static soft_spi_t *lookup(io_owner_t owner, uint32_t handle) {
    unsigned slot = handle & 255u;
    if (!slot || slot > SPI_SLOTS) return NULL;
    soft_spi_t *b = &buses[slot - 1];
    return b->owner == owner && b->handle == handle ? b : NULL;
}
static int delay(soft_spi_t *b) {
    hal_delay_us(b->half_us);
    if (b->cancelled) return IO_ERR_CANCELLED;
    return hal_time_now_us() - b->start_us >= b->timeout_us ? ERR_TIMEOUT : 0;
}
int io_spi_open(io_owner_t owner, const io_spi_config_t *c, uint32_t *handle) {
    if (!c || !handle || c->mode > 3 || c->frequency_hz < 1000 ||
        c->frequency_hz > 500000 || c->transfer_timeout_us > 10000000) return ERR_INVAL;
    if (!io_owner_valid(owner)) return ERR_PERM;
    mutex_lock(&lock);
    int e = ERR_BUSY;
    for (unsigned i = 0; i < SPI_SLOTS && serial < 0xffffffu; i++) {
        if (buses[i].owner) continue;
        int pins[4] = {c->sck, c->mosi, c->miso, c->cs};
        e = io_pins_claim(owner, pins, 4, IO_USE_SPI);
        if (e) break;
        soft_spi_t *b = &buses[i];
        *b = (soft_spi_t){.owner = owner, .handle = (++serial << 8) | (i + 1),
            .sck = c->sck, .mosi = c->mosi, .miso = c->miso, .cs = c->cs,
            .mode = c->mode,
            .half_us = (500000u + c->frequency_hz - 1) / c->frequency_hz,
            .timeout_us = c->transfer_timeout_us ? c->transfer_timeout_us : 2000000};
        hal_io_gpio_output(b->cs, 1);
        hal_io_gpio_output(b->sck, (b->mode >> 1) & 1);
        hal_io_gpio_output(b->mosi, 0);
        hal_io_gpio_input(b->miso, IO_PULL_NONE);
        *handle = b->handle;
        e = 0;
        break;
    }
    mutex_unlock(&lock);
    return e;
}
int io_spi_transfer(io_owner_t owner, uint32_t handle, const uint8_t *tx,
                    uint8_t *rx, size_t length) {
    if (length > 4096) return ERR_INVAL;
    if (!io_owner_valid(owner)) return ERR_PERM;
    mutex_lock(&lock);
    soft_spi_t *b = io_owner_valid(owner) ? lookup(owner, handle) : NULL;
    int e = b ? 0 : ERR_BADF;
    if (b) {
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
        int idle = (b->mode >> 1) & 1;
        bool phase = b->mode & 1;
        hal_gpio_write(b->cs, 1); /* Also terminates a killed caller's partial transfer. */
        hal_gpio_write(b->sck, idle);
        hal_delay_us(b->half_us);
        hal_gpio_write(b->cs, 0);
        for (size_t i = 0; !e && i < length; i++) {
            uint8_t input = 0, output = tx ? tx[i] : 0xff;
            for (unsigned bit = 0; !e && bit < 8; bit++) {
                if (!phase) hal_gpio_write(b->mosi, !!(output & 0x80));
                e = delay(b);
                if (e) break;
                hal_gpio_write(b->sck, !idle);
                if (phase) hal_gpio_write(b->mosi, !!(output & 0x80));
                else input = (uint8_t)((input << 1) | !!hal_gpio_read(b->miso));
                e = delay(b);
                if (e) break;
                hal_gpio_write(b->sck, idle);
                if (phase) input = (uint8_t)((input << 1) | !!hal_gpio_read(b->miso));
                output <<= 1;
            }
            if (!e && rx) rx[i] = input;
        }
        hal_gpio_write(b->sck, idle);
        hal_gpio_write(b->cs, 1);
        b->running = false;
    }
    mutex_unlock(&lock);
    return e;
}
int io_spi_cancel(io_owner_t owner, uint32_t handle) {
    if (!io_owner_valid(owner)) return ERR_PERM;
    uint32_t irq = arch_irq_disable();
    soft_spi_t *b = lookup(owner, handle);
    int e = !b ? ERR_BADF : !b->running ? ERR_BUSY : 0;
    if (!e) b->cancelled = true;
    arch_irq_restore(irq);
    return e;
}
static void close_bus(soft_spi_t *b) {
    /* Publish an invalid slot before releasing pins to another owner. */
    soft_spi_t old = *b;
    uint32_t irq = arch_irq_disable();
    b->owner = b->handle = 0;
    b->running = false;
    arch_irq_restore(irq);
    int pins[4] = {old.sck, old.mosi, old.miso, old.cs};
    hal_gpio_write(old.cs, 1);
    io_pins_release(old.owner, pins, 4);
}

int io_spi_close(io_owner_t owner, uint32_t handle) {
    if (!io_owner_valid(owner)) return ERR_PERM;
    io_spi_cancel(owner, handle);
    mutex_lock(&lock);
    soft_spi_t *b = io_owner_valid(owner) ? lookup(owner, handle) : NULL;
    int e = b ? 0 : ERR_BADF;
    if (b) close_bus(b);
    mutex_unlock(&lock);
    return e;
}
void io_spi_release_owner(io_owner_t owner) {
    uint32_t irq = arch_irq_disable();
    for (unsigned i = 0; i < SPI_SLOTS; i++)
        if (buses[i].owner == owner) buses[i].cancelled = true;
    arch_irq_restore(irq);
    mutex_lock(&lock);
    for (unsigned i = 0; i < SPI_SLOTS; i++)
        if (buses[i].owner == owner && owner) close_bus(&buses[i]);
    mutex_unlock(&lock);
}
