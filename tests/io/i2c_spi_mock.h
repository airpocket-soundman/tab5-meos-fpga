#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <setjmp.h>
#include <string.h>
#include <m5os/io.h>
#include <m5os/mutex.h>
#include <m5os/hal_gpio.h>
#include <m5os/hal_io_gpio.h>
#include <m5os/hal_time.h>
#include <m5os/arch.h>
static uint64_t now;
static io_owner_t pins_owned[IO_PIN_COUNT];
static int levels[IO_PIN_COUNT];
static unsigned claims, releases, pin_touches;
static void (*on_release)(void);
static jmp_buf killed;
static void die_after_pin_release(void) { on_release=NULL; longjmp(killed,1); }
static void (*on_delay)(void);
static bool owner_alive = true;
static void (*on_owner_check)(void);
bool io_owner_valid(io_owner_t owner) { if(on_owner_check)on_owner_check(); return owner_alive && (owner == 1 || owner == 2); }
int io_pins_claim(io_owner_t owner,const int *pins,size_t count,io_use_t use) {
    (void)use;
    if (!io_owner_valid(owner)) return ERR_PERM;
    for(size_t i=0;i<count;i++) {
        if(pins[i]<0 || pins[i]>=IO_PIN_COUNT) return ERR_INVAL;
        if(pins[i]==31 || pins[i]==32 || pins[i]==39 || pins[i]==42 || pins[i]==43 || pins[i]==44) return ERR_PERM;
        if(pins_owned[pins[i]]) return ERR_BUSY;
        for(size_t j=0;j<i;j++) if(pins[j]==pins[i]) return ERR_INVAL;
    }
    for(size_t i=0;i<count;i++) pins_owned[pins[i]]=owner;
    claims++;
    return 0;
}
int io_pins_release(io_owner_t owner,const int *pins,size_t count) {
    for(size_t i=0;i<count;i++) assert(pins_owned[pins[i]]==owner);
    for(size_t i=0;i<count;i++) { pins_owned[pins[i]]=0; levels[pins[i]]=1; }
    releases++;
    if(on_release)on_release();
    return 0;
}
void mutex_lock(mutex_t *m) { assert(!m->owner); m->owner=(task_t *)(uintptr_t)1; }
void mutex_unlock(mutex_t *m) { assert(m->owner); m->owner=NULL; }
uint32_t arch_irq_disable(void) { return 0; }
void arch_irq_restore(uint32_t flags) { (void)flags; }
uint64_t hal_time_now_us(void) { return now; }
void hal_delay_us(uint32_t us) { now+=us; if(on_delay)on_delay(); }
static void changed(int pin,int value);
void hal_gpio_config_open_drain(int pin) { assert(pins_owned[pin]); levels[pin]=1; }
void hal_gpio_od_release(int pin) { pin_touches++; changed(pin,1); }
void hal_gpio_od_drive_low(int pin) { pin_touches++; changed(pin,0); }
void hal_gpio_write(int pin,int value) { pin_touches++; changed(pin,!!value); }
void hal_gpio_config_output(int pin,int initial) { assert(pins_owned[pin]); changed(pin,!!initial); }
void hal_io_gpio_output(int pin,int initial) { hal_gpio_config_output(pin,initial); }
void hal_io_gpio_input(int pin,int pull) { assert(pins_owned[pin] && pull==IO_PULL_NONE); }
void hal_io_gpio_release(int pin) { (void)pin; }
