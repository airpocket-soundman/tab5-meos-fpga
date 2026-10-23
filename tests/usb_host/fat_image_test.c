/* Host-only FAT integration test using a generated FAT12 image; no USB writes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ff.h"
#include "diskio.h"
static unsigned char disk[1474560];
DSTATUS disk_initialize(BYTE p){return p?STA_NOINIT:0;}
DSTATUS disk_status(BYTE p){return p?STA_NOINIT:0;}
DRESULT disk_read(BYTE p,BYTE*b,LBA_t l,UINT n){if(p||l>=2880||n>2880-l)return RES_PARERR;memcpy(b,disk+l*512,n*512);return RES_OK;}
DRESULT disk_write(BYTE p,const BYTE*b,LBA_t l,UINT n){if(p||l>=2880||n>2880-l)return RES_PARERR;memcpy(disk+l*512,b,n*512);return RES_OK;}
DRESULT disk_ioctl(BYTE p,BYTE c,void*b){if(p)return RES_PARERR;if(c==CTRL_SYNC)return RES_OK;if(c==GET_SECTOR_COUNT){*(LBA_t*)b=2880;return RES_OK;}if(c==GET_SECTOR_SIZE){*(WORD*)b=512;return RES_OK;}if(c==GET_BLOCK_SIZE){*(DWORD*)b=1;return RES_OK;}return RES_PARERR;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"failure line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char**argv){
 CHECK(argc==2);FILE*in=fopen(argv[1],"rb");CHECK(in);CHECK(fread(disk,1,sizeof disk,in)==sizeof disk);fclose(in);
 FATFS fs;FIL f,g;UINT n;char buf[128];
 CHECK(f_mount(&fs,"",1)==FR_OK);
 CHECK(f_open(&f,"HELLO.TXT",FA_READ)==FR_OK);CHECK(f_read(&f,buf,100,&n)==FR_OK);CHECK(n==6 && !memcmp(buf,"hello\n",6));CHECK(f_close(&f)==FR_OK);
 CHECK(f_mkdir("check")==FR_OK);
 CHECK(f_open(&f,"check/long-file-name.txt",FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
 CHECK(f_write(&f,"roundtrip",9,&n)==FR_OK && n==9);CHECK(f_sync(&f)==FR_OK);
 CHECK(f_open(&g,"check/long-file-name.txt",FA_WRITE)==FR_LOCKED);CHECK(f_close(&f)==FR_OK);
 CHECK(f_mount(NULL,"",0)==FR_OK);CHECK(f_mount(&fs,"",1)==FR_OK);
 CHECK(f_open(&f,"check/long-file-name.txt",FA_READ)==FR_OK);CHECK(f_read(&f,buf,100,&n)==FR_OK && n==9 && !memcmp(buf,"roundtrip",9));CHECK(f_close(&f)==FR_OK);
 FF_DIR d;FILINFO info;CHECK(f_opendir(&d,"check")==FR_OK);CHECK(f_readdir(&d,&info)==FR_OK);CHECK(!strcmp(info.fname,"long-file-name.txt"));CHECK(f_closedir(&d)==FR_OK);
 CHECK(f_unlink("check/long-file-name.txt")==FR_OK);CHECK(f_unlink("check")==FR_OK);
 puts("PASS: read, create, long filename, write/sync, duplicate writer rejection, remount, directory listing, delete");return 0;
}
