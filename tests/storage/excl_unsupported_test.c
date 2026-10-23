/* Unsupported backends must reject EXCL before locks/device side effects. */
#include <assert.h>
#include <stdio.h>
#include <m5os/vfs.h>
#include <m5os/errno.h>
#include <m5os/device.h>
#include <m5os/mutex.h>
void mutex_lock(mutex_t *m){(void)m;assert(0);}
void mutex_unlock(mutex_t *m){(void)m;assert(0);}
void *kmalloc(size_t n){(void)n;assert(0);return 0;}
void *kmalloc_psram(size_t n){(void)n;assert(0);return 0;}
void *kcalloc(size_t n,size_t s){(void)n;(void)s;assert(0);return 0;}
void kfree(void *p){(void)p;assert(0);}
uint32_t crc32(const void *p,size_t n){(void)p;(void)n;assert(0);return 0;}
int device_open(const char *p){(void)p;assert(0);return 0;}
int device_close(int h){(void)h;assert(0);return 0;}
int device_read(int h,void *b,size_t n){(void)h;(void)b;(void)n;assert(0);return 0;}
int device_write(int h,const void *b,size_t n){(void)h;(void)b;(void)n;assert(0);return 0;}
int device_ioctl(int h,uint32_t c,void *a){(void)h;(void)c;(void)a;assert(0);return 0;}
int device_foreach(int (*cb)(const device_t*,void*),void *a){(void)cb;(void)a;assert(0);return 0;}
int main(void){
    uint32_t flags=VFS_O_WRITE|VFS_O_CREATE|VFS_O_EXCL;
    void *out=0;
    assert(fs_open("/existing",flags)==ERR_NOSYS);
    assert(fs_open("/existing",flags|VFS_O_TRUNC)==ERR_NOSYS);
    assert(vfs_devfs.open(0,"/uart",flags,&out)==ERR_NOSYS);
    assert(vfs_devfs.open(0,"/uart",flags|VFS_O_TRUNC,&out)==ERR_NOSYS);
    puts("M5FS/devfs reject unsupported EXCL before all side effects PASS");
}
