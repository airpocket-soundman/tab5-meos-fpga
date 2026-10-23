#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "../../drivers/bluetooth/npl/meos_allocator.inc"
static bool fail_all,psram_missing;
static unsigned frees;
void *kmalloc_psram(size_t n) {return fail_all||psram_missing?NULL:malloc(n);}
void *kmalloc(size_t n) {return fail_all?NULL:malloc(n);}
void kfree(void *raw) {
 size_t n=*(size_t *)raw;const uint8_t *p=(const uint8_t *)raw+16;
 for(size_t i=0;i<n;i++)assert(p[i]==0);
 frees++;free(raw);
}
int main(void) {
 assert(!meos_bt_malloc(0));assert(!meos_bt_malloc(SIZE_MAX));assert(!meos_bt_calloc(SIZE_MAX,2));
 uint8_t *p=meos_bt_calloc(16,1);assert(p&&((uintptr_t)p&15)==0);
 for(unsigned i=0;i<16;i++)assert(!p[i]);
 memset(p,0x75,16);fail_all=true;assert(!meos_bt_realloc(p,32));assert(p[15]==0x75);
 fail_all=false;uint8_t *next=meos_bt_realloc(p,32);assert(next&&next[15]==0x75&&frees==1);
 memset(next,0x44,32);next=meos_bt_realloc(next,8);assert(next&&next[7]==0x44&&frees==2);
 assert(!meos_bt_realloc(next,0)&&frees==3);
 psram_missing=true;p=meos_bt_malloc(7);assert(p);memset(p,1,7);meos_bt_free(p);assert(frees==4);
 meos_bt_free(NULL);puts("NimBLE MeOS allocator overflow/preserve/wipe tests PASS");
}
