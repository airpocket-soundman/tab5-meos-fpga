/* Size-tagged MeOS heap for NimBLE only. No IDF/FreeRTOS allocator fallback. */
#include <m5os/kmalloc.h>
#include <stdint.h>
#include <string.h>
void *meos_bt_malloc(size_t n) {
    if(!n||n>SIZE_MAX-16)return NULL;
    uint8_t *raw=kmalloc_psram(n+16);
    if(!raw)raw=kmalloc(n+16);
    if(!raw)return NULL;
    *(size_t *)raw=n;return raw+16;
}
void meos_bt_free(void *p) {
    if(!p)return;
    uint8_t *raw=(uint8_t *)p-16;size_t n=*(size_t *)raw;
    volatile uint8_t *wipe=p;for(size_t i=0;i<n;i++)wipe[i]=0;
    kfree(raw);
}
void *meos_bt_calloc(size_t n,size_t size) {
    if(size&&n>SIZE_MAX/size)return NULL;
    size_t bytes=n*size;void *p=meos_bt_malloc(bytes);if(p)memset(p,0,bytes);return p;
}
void *meos_bt_realloc(void *old,size_t n) {
    if(!old)return meos_bt_malloc(n);
    if(!n) {meos_bt_free(old);return NULL;}
    size_t previous=*(size_t *)((uint8_t *)old-16);
    void *p=meos_bt_malloc(n);if(!p)return NULL;
    memcpy(p,old,n<previous?n:previous);meos_bt_free(old);return p;
}
