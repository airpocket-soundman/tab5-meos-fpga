#include <m5os/arch.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
void arch_task_trampoline(void) {}
static void entry(void *p) {(void)p;}
#include "../../hal/esp32p4/context_init.c"
int main(void) {
    _Static_assert(sizeof(uintptr_t)==4,"test must use32-bit pointers");
    unsigned char storage[512]; memset(storage,0xa5,sizeof(storage));
    for(unsigned offset=0;offset<16;offset++) {
        uint32_t *sp=arch_task_stack_init(storage+offset,400,entry,(void *)(uintptr_t)0x12345678);
        uintptr_t top=((uintptr_t)storage+offset+400)&~(uintptr_t)15;
        assert((uintptr_t)sp==top-128 && ((uintptr_t)sp&15)==0);
        assert(sp[0]==(uint32_t)(uintptr_t)arch_task_trampoline);
        assert(sp[1]==(uint32_t)(uintptr_t)entry && sp[2]==0x12345678);
        for(unsigned n=3;n<32;n++)assert(sp[n]==0);
    }
    puts("PASS:32-bit initial context alignment, register slots, FP/fcsr zero initialization");
}
