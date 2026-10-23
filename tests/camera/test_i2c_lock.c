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
static bool system_only, scripted;
static uint8_t incoming[32], outgoing[16];
static unsigned incoming_count, incoming_pos, wire_bits, wire_value, outgoing_count;
static bool wire_active;
static void clock_rise(void) {
    if (!scripted || !wire_active) return;
    if (wire_bits < 8) wire_value = (wire_value << 1) | (unsigned)sda_state;
    if (++wire_bits == 9) {
        if (outgoing_count < sizeof outgoing) outgoing[outgoing_count++] = (uint8_t)wire_value;
        wire_bits = wire_value = 0;
    }
}
static void script_read(uint8_t value, bool fail_read) {
    scripted=true; incoming_count=incoming_pos=0; outgoing_count=0; wire_active=false;
    incoming[incoming_count++]=fail_read ? 1 : 0; /* address ACK */
    incoming[incoming_count++]=0; /* register ACK */
    incoming[incoming_count++]=0; /* repeated START/read address ACK */
    for(int bit=7;bit>=0;bit--) incoming[incoming_count++]=(value>>bit)&1;
    incoming[incoming_count++]=0; incoming[incoming_count++]=0; incoming[incoming_count++]=0;
}
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
    if (system_only && p != 32 && p != 31) InterlockedIncrement(&errors);
    if (p == 32) {
        if (!scl_state) clock_rise();
        scl_state = 1;
    }
    else {
        if (!sda_state && scl_state) {
            stops++;
            wire_active=false;
        }
        sda_state = 1;
    }
}
void hal_gpio_od_drive_low(int p) {
    check();
    if (system_only && p != 32 && p != 31) InterlockedIncrement(&errors);
    if (p == 32)
        scl_state = 0;
    else {
        if (sda_state && scl_state) {
            starts++;
            if(scripted) { wire_active=true; wire_bits=wire_value=outgoing_count=0; }
        }
        sda_state = 0;
    }
}
int hal_gpio_read(int p) {
    check();
    if (system_only && p != 32 && p != 31) InterlockedIncrement(&errors);
    if (p == 32) return 1;
    if (scripted && incoming_pos < incoming_count) return incoming[incoming_pos++];
    return 0;
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
    hal_i2c_set_pins(7, 8); /* External keyboard bus selected globally. */
    system_only=true;
    before=locks;
    script_read(0x89,false);
    CHECK(hal_i2c_system_update_reg(0x44,0x05,0x08,false));
    CHECK(locks==before+1 && outgoing_count==3);
    CHECK(outgoing[0]==0x88 && outgoing[1]==0x05 && outgoing[2]==0x81);
    CHECK(s_scl==7 && s_sda==8); /* Must not alter the selected external bus. */
    script_read(0x81,false);
    CHECK(hal_i2c_system_update_reg(0x44,0x05,0x08,true));
    CHECK(outgoing_count==3 && outgoing[2]==0x89); /* Wi-Fi and charger bits survive. */
    script_read(0x89,true);
    starts=stops=0;
    CHECK(!hal_i2c_system_update_reg(0x44,0x05,0x08,false));
    CHECK(starts==1 && stops==1); /* A failed read must not write an unknown latch value. */
    before=locks;
    CHECK(!hal_i2c_system_update_reg(0xff,0x05,0x08,false));
    CHECK(locks==before);
    scripted=system_only=false;
    hal_i2c_set_pins(32,31);
    LONG concurrent_before=locks;
    HANDLE a = CreateThread(NULL, 0, worker, (void *)1, 0, NULL),
           b = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    WaitForSingleObject(a, 30000);
    WaitForSingleObject(b, 30000);
    CloseHandle(a);
    CloseHandle(b);
    CHECK(errors == 0 && locks == concurrent_before+20);
    printf("PASS I2C full transaction ownership, repeated START, fixed system-bus RMW/preserved bits/read failure, concurrent "
           "SCCB/touch reads (%ld locks)\n",
           locks);
    return 0;
}
