#define WIN32_LEAN_AND_MEAN
#include <m5os/mutex.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
static SRWLOCK guard = SRWLOCK_INIT;
static volatile LONG errors, locks;
static DWORD owner;
static bool running;
static int scl_state = 1, sda_state = 1;
static unsigned starts, stops;
bool sched_started(void) { return running; }
void mutex_lock(mutex_t *m) {
    AcquireSRWLockExclusive(&guard);
    owner = GetCurrentThreadId();
    InterlockedIncrement(&locks);
}
void mutex_unlock(mutex_t *m) {
    if (owner != GetCurrentThreadId())
        InterlockedIncrement(&errors);
    owner = 0;
    ReleaseSRWLockExclusive(&guard);
}
static void check(void) {
    if (running && owner != GetCurrentThreadId())
        InterlockedIncrement(&errors);
}
void hal_gpio_config_open_drain(int p) { check(); }
void hal_gpio_od_release(int p) {
    check();
    if (p == 32)
        scl_state = 1;
    else {
        if (!sda_state && scl_state)
            stops++;
        sda_state = 1;
    }
}
void hal_gpio_od_drive_low(int p) {
    check();
    if (p == 32)
        scl_state = 0;
    else {
        if (sda_state && scl_state)
            starts++;
        sda_state = 0;
    }
}
int hal_gpio_read(int p) {
    check();
    return p == 32 ? 1 : 0;
}
void hal_delay_us(uint32_t us) {
    check();
    SwitchToThread();
}
#include "../../hal/common/i2c_bitbang.c"
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            printf("FAIL line %d %s\n", __LINE__, #x);                                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static DWORD WINAPI worker(void *arg) {
    uint8_t bytes[70];
    for (int i = 0; i < 10; i++) {
        if (arg)
            hal_i2c_read_reg16(0x36, 0x3107, bytes, 2);
        else
            hal_i2c_read_reg16(0x55, 0x0100, bytes, sizeof(bytes));
    }
    return 0;
}
int main(void) {
    hal_i2c_init();
    CHECK(locks == 0);
    running = true;
    uint8_t v;
    starts = stops = 0;
    CHECK(hal_i2c_read_reg16(0x36, 0x3107, &v, 1));
    CHECK(starts == 2 && stops == 1 && locks == 1);
    LONG before = locks;
    starts = stops = 0;
    CHECK(hal_i2c_bit_on(0x43, 0x05, 1));
    CHECK(starts == 3 && stops == 2 && locks == before + 1);
    CHECK(!hal_i2c_read_reg16(0x36, 0, NULL, 1));
    CHECK(!hal_i2c_read_reg(0x36, 0, NULL));
    CHECK(!hal_i2c_write_reg(0xff, 0, 0));
    HANDLE a = CreateThread(NULL, 0, worker, (void *)1, 0, NULL),
           b = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    WaitForSingleObject(a, 30000);
    WaitForSingleObject(b, 30000);
    CloseHandle(a);
    CloseHandle(b);
    CHECK(errors == 0 && locks == 22);
    printf("PASS I2C full transaction ownership, repeated START, atomic bit RMW, concurrent "
           "SCCB/touch reads (%ld locks)\n",
           locks);
    return 0;
}
