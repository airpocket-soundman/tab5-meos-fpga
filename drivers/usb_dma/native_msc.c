/* USB Mass Storage Bulk-Only Transport, SCSI transparent subclass, one LUN.
 * No formatting; writes occur only when requested through the filesystem. */
#include "native_msc.h"
#include "hal_usbh_dma.h"
#include <m5os/hal_time.h>
#include <m5os/task.h>
#include <m5os/mutex.h>
#include <m5os/errno.h>
#include <m5os/kprintf.h>
#include <string.h>
static uint8_t s_in, s_out;
static uint16_t s_imps, s_omps;
static bool s_ready;
static uint32_t s_tag;
static mutex_t s_lock = MUTEX_INIT;
static blockdev_t s_bd;
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static void putbe(uint8_t *p,uint32_t n) { p[0]=n>>24; p[1]=n>>16; p[2]=n>>8; p[3]=n; }
static void putle(uint8_t *p,uint32_t n) { p[0]=n; p[1]=n>>8; p[2]=n>>16; p[3]=n>>24; }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static int bulk(bool in,void *buf,size_t n)
{
    if (!hal_usbh_data_open(1,in?s_in:s_out,in?s_imps:s_omps,false,0)) return ERR_IO;
    if (hal_usbh_data_start(buf,n)<0) { hal_usbh_data_close(); return ERR_IO; }
    uint64_t start=hal_time_now_us(); int r;
    while ((r=hal_usbh_data_poll())==USBH_PENDING) {
        if (!hal_usbh_connected() || hal_time_now_us()-start>3000000u) { kprintf("native-dma: bulk timeout ep=%02x bytes=%u\n",in?s_in:s_out,(unsigned)n); hal_usbh_dump(); hal_usbh_data_close(); return ERR_IO; }
        task_sleep(1);
    }
    hal_usbh_data_close();
    if(r!=(int)n)kprintf("native-dma: bulk result %d expected=%u ep=%02x\n",r,(unsigned)n,in?s_in:s_out);
    return r;
}
static int scsi(const uint8_t *cdb,int cdbn,void *data,uint32_t n,bool in)
{
    uint8_t cbw[31]={0},csw[13]; uint32_t tag=++s_tag;
    putle(cbw,0x43425355);putle(cbw+4,tag);putle(cbw+8,n);cbw[12]=in?0x80:0;cbw[14]=cdbn;memcpy(cbw+15,cdb,cdbn);
    if (bulk(false,cbw,31)!=31) return ERR_IO;
    for (uint32_t off=0;off<n;) {
        uint32_t part=n-off>4096?4096:n-off;
        int r=bulk(in,(uint8_t*)data+off,part);
        if (r!=(int)part) return ERR_IO;
        off+=part;
    }
    if (bulk(true,csw,13)!=13 || le32(csw)!=0x53425355 || le32(csw+4)!=tag || le32(csw+8)>n) return ERR_IO;
    return csw[12]==0 && le32(csw+8)==0 ? ERR_OK : ERR_IO;
}
static int rw(blockdev_t *bd,uint32_t lba,uint32_t count,void *buf,bool write)
{
    if (!buf || !count || lba>=bd->block_count || count>bd->block_count-lba) return ERR_INVAL;
    mutex_lock(&s_lock); int rc=ERR_NODEV;
    if (s_ready) {
        rc=0;
        for (uint32_t i=0;i<count && rc==0;) {
            uint32_t n=count-i>8?8:count-i;
            uint8_t cmd[10]={0};cmd[0]=write?0x2a:0x28;putbe(cmd+2,lba+i);cmd[7]=n>>8;cmd[8]=n;
            rc=scsi(cmd,10,(uint8_t*)buf+i*512,n*512,!write);i+=n;
        }
    }
    mutex_unlock(&s_lock);return rc;
}
static int rd(blockdev_t *b,uint32_t l,uint32_t n,void *p){return rw(b,l,n,p,false);}
static int wr(blockdev_t *b,uint32_t l,uint32_t n,const void *p){return rw(b,l,n,(void*)p,true);}
blockdev_t *usbmsc_device(void){return s_ready?&s_bd:NULL;}
void usbmsc_detach(void){mutex_lock(&s_lock);s_ready=false;hal_usbh_data_close();mutex_unlock(&s_lock);}
bool usbmsc_attach(uint8_t iface,uint8_t in,uint16_t imps,uint8_t out,uint16_t omps)
{
    (void)iface;s_in=in;s_out=out;s_imps=imps;s_omps=omps;s_ready=false;
    uint8_t cap[8],cmd[10]={0x25};
    /* Some devices need a TUR / REQUEST SENSE sequence before READ CAPACITY. */
    int rc=ERR_IO;
    for(int i=0;i<3 && rc!=0;i++) {
        uint8_t tur[6]={0},sensecmd[6]={3,0,0,0,18,0},sense[18];
        if(scsi(tur,6,NULL,0,true)!=0) scsi(sensecmd,6,sense,18,true);
        rc=scsi(cmd,10,cap,8,true);if(rc)task_sleep(100);
    }
    if(rc || be32(cap+4)!=512 || be32(cap)==0xffffffffu) {kprintf("usbmsc: unsupported capacity or transport error\n");return false;}
    s_bd=(blockdev_t){.name="usb",.block_size=512,.block_count=be32(cap)+1,.read=rd,.write=wr};s_ready=true;
    kprintf("usbmsc: ready, %u sectors of 512 bytes\n",s_bd.block_count);return true;
}
