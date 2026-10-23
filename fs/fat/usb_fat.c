/* FatFs adapter for a removable USB drive. All operations serialized by MeOS.
 * No automatic format. On removal, stale handles fail until explicitly closed. */
#include "usb_fat.h"
#include "../../third_party/fatfs/ff.h"
#include "../../third_party/fatfs/diskio.h"
#include <m5os/vfs.h>
#include <m5os/app.h>
#include <m5os/kmalloc.h>
#include <m5os/mutex.h>
#include <m5os/errno.h>
#include <m5os/kprintf.h>
#include <string.h>
static FATFS s_fs;
static blockdev_t *s_bd;
static bool s_live,s_mounted;
static unsigned s_files;
static mutex_t s_lock=MUTEX_INIT;
static int err(FRESULT r){switch(r){case FR_OK:return 0;case FR_NO_FILE:case FR_NO_PATH:return ERR_NOENT;case FR_EXIST:return ERR_EXIST;case FR_WRITE_PROTECTED:case FR_DENIED:return ERR_PERM;case FR_INVALID_NAME:return ERR_INVAL;case FR_NOT_ENOUGH_CORE:return ERR_NOMEM;default:return ERR_IO;}}
DSTATUS disk_initialize(BYTE p){return p==0 && s_bd?0:STA_NOINIT;}
DSTATUS disk_status(BYTE p){return p==0 && s_bd?0:STA_NOINIT;}
DRESULT disk_read(BYTE p,BYTE *buf,LBA_t lba,UINT n){return p==0 && s_bd && s_bd->read(s_bd,lba,n,buf)==0?RES_OK:RES_ERROR;}
DRESULT disk_write(BYTE p,const BYTE *buf,LBA_t lba,UINT n){return p==0 && s_bd && s_bd->write(s_bd,lba,n,buf)==0?RES_OK:RES_ERROR;}
DRESULT disk_ioctl(BYTE p,BYTE cmd,void *buf){
 if(p||!s_bd)return RES_NOTRDY;
 switch(cmd){case CTRL_SYNC:return RES_OK;case GET_SECTOR_COUNT:*(LBA_t*)buf=s_bd->block_count;return RES_OK;case GET_SECTOR_SIZE:*(WORD*)buf=512;return RES_OK;case GET_BLOCK_SIZE:*(DWORD*)buf=1;return RES_OK;default:return RES_PARERR;}}
static int op(void *d,const char *path,uint32_t flags,void **out){
 (void)d;
 if((flags&VFS_O_READ)&&!task_has_perm(APP_PERM_FS_READ))return ERR_PERM;
 if((flags&(VFS_O_WRITE|VFS_O_CREATE|VFS_O_TRUNC|VFS_O_APPEND))&&!task_has_perm(APP_PERM_FS_WRITE))return ERR_PERM;
 mutex_lock(&s_lock);int rc=ERR_NODEV;FIL *f=NULL;
 if(s_live){f=kmalloc_psram(sizeof *f);if(!f)rc=ERR_NOMEM;else{
  memset(f,0,sizeof *f);
  BYTE mode=(flags&VFS_O_READ?FA_READ:0)|(flags&VFS_O_WRITE?FA_WRITE:0);
  if(flags&VFS_O_TRUNC)mode|=FA_CREATE_ALWAYS;else if(flags&VFS_O_CREATE)mode|=FA_OPEN_ALWAYS;
  rc=err(f_open(f,path,mode));if(!rc && (flags&VFS_O_APPEND))rc=err(f_lseek(f,f_size(f)));
  if(!rc){*out=f;s_files++;}else {f_close(f);kfree(f);}
 }}mutex_unlock(&s_lock);return rc;}
static int cl(void *p){mutex_lock(&s_lock);int rc=s_live?err(f_close(p)):ERR_NODEV;kfree(p);if(s_files)s_files--;mutex_unlock(&s_lock);return rc;}
static int rd(void *p,void *buf,size_t n){mutex_lock(&s_lock);UINT got=0;int rc=s_live?err(f_read(p,buf,n,&got)):ERR_NODEV;mutex_unlock(&s_lock);return rc?rc:(int)got;}
static int wr(void *p,const void *buf,size_t n){mutex_lock(&s_lock);UINT got=0;int rc=s_live?err(f_write(p,buf,n,&got)):ERR_NODEV;if(!rc)rc=err(f_sync(p));mutex_unlock(&s_lock);return rc?rc:(int)got;}
static int sk(void *p,int32_t off,int whence){mutex_lock(&s_lock);FIL*f=p;int rc=ERR_NODEV;if(s_live){int64_t base=whence==0?0:whence==1?f_tell(f):whence==2?f_size(f):-1;int64_t pos=base+off;rc=base<0||pos<0||pos>0x7fffffff?ERR_INVAL:err(f_lseek(f,pos));if(!rc)rc=f_tell(f);}mutex_unlock(&s_lock);return rc;}
static void info(fs_info_t *o,const FILINFO *i){memset(o,0,sizeof *o);const char *name=strlen(i->fname)>FS_NAME_MAX && i->altname[0]?i->altname:i->fname;size_t n=strlen(name);if(n>sizeof o->name-1)n=sizeof o->name-1;memcpy(o->name,name,n);o->type=(i->fattrib&AM_DIR)?FS_TYPE_DIR:FS_TYPE_FILE;o->size=i->fsize;}
static int st(void*d,const char*path,vfs_info_t*out){(void)d;if(!task_has_perm(APP_PERM_FS_READ))return ERR_PERM;mutex_lock(&s_lock);FILINFO i;int rc=ERR_NODEV;if(s_live){if(!strcmp(path,"/")){memset(out,0,sizeof *out);out->type=FS_TYPE_DIR;rc=0;}else{rc=err(f_stat(path,&i));if(!rc)info(out,&i);}}mutex_unlock(&s_lock);return rc;}
static int ls(void*d,const char*path,int(*cb)(const vfs_info_t*,void*),void*ctx){(void)d;if(!task_has_perm(APP_PERM_FS_READ))return ERR_PERM;mutex_lock(&s_lock);FF_DIR dir;FILINFO i;int rc=s_live?err(f_opendir(&dir,path)):ERR_NODEV;int count=0;if(!rc){for(;;){rc=err(f_readdir(&dir,&i));if(rc||!i.fname[0])break;vfs_info_t o;info(&o,&i);count++;if(cb(&o,ctx))break;}f_closedir(&dir);}mutex_unlock(&s_lock);return rc?rc:count;}
static int mk(void*d,const char*p){(void)d;if(!task_has_perm(APP_PERM_FS_WRITE))return ERR_PERM;mutex_lock(&s_lock);int rc=s_live?err(f_mkdir(p)):ERR_NODEV;mutex_unlock(&s_lock);return rc;}
static int un(void*d,const char*p){(void)d;if(!task_has_perm(APP_PERM_FS_WRITE))return ERR_PERM;mutex_lock(&s_lock);int rc=s_live?err(f_unlink(p)):ERR_NODEV;mutex_unlock(&s_lock);return rc;}
static const vfs_fs_t ops={.name="fat",.open=op,.close=cl,.read=rd,.write=wr,.seek=sk,.stat=st,.readdir=ls,.mkdir=mk,.unlink=un};
void usb_fat_detach(void){mutex_lock(&s_lock);s_live=false;s_bd=NULL;if(!s_files){f_mount(NULL,"",0);if(s_mounted&&vfs_umount("/usb")==0)s_mounted=false;}mutex_unlock(&s_lock);}
bool usb_fat_mount(blockdev_t*bd){mutex_lock(&s_lock);if(s_files){kprintf("usb: close old file handles before reconnecting\n");mutex_unlock(&s_lock);return false;}s_bd=bd;int rc=err(f_mount(&s_fs,"",1));if(!rc){rc=s_mounted?0:vfs_mount("/usb",&ops,NULL);if(!rc){s_live=s_mounted=true;kprintf("usb: FAT mounted at /usb\n");}}if(rc){s_bd=NULL;f_mount(NULL,"",0);kprintf("usb: FAT mount failed (%d), no format attempted\n",rc);}mutex_unlock(&s_lock);return rc==0;}
