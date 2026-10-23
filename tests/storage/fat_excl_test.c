/* Actual MeOS FAT adapter + imported FatFs on disposable RAM FAT image. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../fs/fat/usb_fat.c"
static unsigned char disk[1474560];
static unsigned lock_depth;
void mutex_lock(mutex_t *m){assert(m==&s_lock && !lock_depth);lock_depth++;}
void mutex_unlock(mutex_t *m){assert(m==&s_lock && lock_depth);lock_depth--;}
void *kmalloc_psram(size_t n){return malloc(n);}
void kfree(void *p){free(p);}
bool task_has_perm(uint32_t p){(void)p;return true;}
int ksnprintf(char *b,size_t n,const char *f,...){va_list a;va_start(a,f);int r=vsnprintf(b,n,f,a);va_end(a);return r;}
void kprintf(const char *f,...){(void)f;}
int vfs_mount(const char *p,const vfs_fs_t *fs,void *d){(void)p;(void)fs;(void)d;return 0;}
int vfs_umount(const char *p){(void)p;return 0;}
static int read_bd(blockdev_t *bd,uint32_t lba,uint32_t n,void *p){(void)bd;assert(lock_depth);if(lba>=2880 || n>2880-lba)return -1;memcpy(p,disk+lba*512,n*512);return 0;}
static int write_bd(blockdev_t *bd,uint32_t lba,uint32_t n,const void *p){(void)bd;assert(lock_depth);if(lba>=2880 || n>2880-lba)return -1;memcpy(disk+lba*512,p,n*512);return 0;}
int main(int argc,char **argv){
    assert(argc==2);FILE *in=fopen(argv[1],"rb");assert(in);assert(fread(disk,1,sizeof disk,in)==sizeof disk);fclose(in);
    blockdev_t bd={.block_size=512,.block_count=2880,.read=read_bd,.write=write_bd};
    assert(usb_fat_mount(&bd));volume_t *v=&volumes[0];void *a=0,*b=0;char text[32]={0};
    uint32_t exclusive=VFS_O_WRITE|VFS_O_CREATE|VFS_O_EXCL;
    assert(op(v,"/HELLO.TXT",exclusive,&a)==ERR_EXIST);
    assert(op(v,"/HELLO.TXT",exclusive|VFS_O_TRUNC,&a)==ERR_EXIST);
    assert(!op(v,"/HELLO.TXT",VFS_O_READ,&a));assert(rd(a,text,sizeof text)==6 && !memcmp(text,"hello\n",6));assert(!cl(a));
    assert(op(v,"/invalid",VFS_O_EXCL,&a)==ERR_INVAL);
    assert(op(v,"/invalid",VFS_O_EXCL|VFS_O_CREATE,&a)==ERR_INVAL);
    assert(op(v,"/invalid",VFS_O_EXCL|VFS_O_WRITE,&a)==ERR_INVAL);
    assert(!op(v,"/new.txt",exclusive,&a));assert(wr(a,"winner",6)==6);
    int duplicate=op(v,"/new.txt",exclusive|VFS_O_TRUNC,&b);
    assert(duplicate==ERR_BUSY || duplicate==ERR_EXIST); /* FatFs writer lock can precede EXIST. */
    assert(!cl(a));assert(op(v,"/new.txt",exclusive,&b)==ERR_EXIST);
    assert(!op(v,"/new.txt",VFS_O_READ,&a));memset(text,0,sizeof text);
    assert(rd(a,text,sizeof text)==6 && !memcmp(text,"winner",6));assert(!cl(a));
    assert(!op(v,"/new-trunc.txt",exclusive|VFS_O_TRUNC,&a));assert(wr(a,"new",3)==3);assert(!cl(a));
    assert(op(v,"/new-trunc.txt",exclusive|VFS_O_TRUNC,&a)==ERR_EXIST);
    assert(!v->files && !lock_depth);
    puts("FAT adapter EXCL: existing bytes preserved, atomic new create, duplicate writer, invalid flags PASS");
}
