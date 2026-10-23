#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "../../drivers/wifi/hosted_wire.h"
#define ESP_SLAVE_TOKEN_RDATA 0x44u
#define ESP_SLAVE_PKT_LEN_REG 0x60u
#define ESP_SLAVE_CMD53_END 0x1f800u
#define SDIO_BLOCK_SIZE 512u
static uint32_t s_sdio_tx_bufs, s_sdio_rx_bytes;
static uint8_t s_tx_packet_buf[2048], sent[1536];
static bool write_ok = true;
static uint32_t write_address;
static size_t write_length;
static void mem_zero(void *p,size_t n) { memset(p,0,n); }
static void mem_copy(void *p,const void *q,size_t n) { memcpy(p,q,n); }
static uint16_t compute_checksum(const uint8_t *p,size_t n)
{ uint16_t sum=0;for(size_t i=0;i<n;i++)sum+=p[i];return sum; }
static bool sdio_reg_read(uint32_t reg,void *p,size_t n)
{ assert((reg==0x44 || reg==0x60) && n==4); *(uint32_t*)p=reg==0x44 ? (s_sdio_tx_bufs+1)<<16 : 0;return true; }
static bool sdio_cmd53(bool write,uint8_t fn,uint32_t addr,bool block,void *buf,size_t n)
{ assert(write && fn==1 && block && n%512==0 && n<=sizeof sent);write_address=addr;write_length=n;memcpy(sent,buf,n);return write_ok; }
static void esp_rom_delay_us(unsigned n) { (void)n; }
static void kprintf(const char *fmt,...) { (void)fmt; }
static uint64_t hal_time_now_us(void) { static uint64_t t; return t+=10000; }
static void task_sleep(uint32_t ms) { (void)ms; }
static void sdio_poll_rx_locked(void) { }
#include "../../drivers/wifi/ng_updater.inc"
int main(void)
{
    uint8_t ng[40]={3,0,0,0,28,0,12,0,0,0,0,0,
        1,0,21,0,20,0,0,0,3,1,13,0,1,1,
        1,12,'N','G',0,1,0,3,0,0,1,0,0,0};
    assert(hosted_wire_is_ng_boot(ng,sizeof ng)); ng_capture_boot(ng);
    assert(s_ng.valid && s_ng.version_valid && s_ng.chip==13 && s_ng.capabilities==1);
    assert(s_ng.version[0]==1 && s_ng.version[2]==3 && s_ng.reset_reason==1);
    assert(ng_send_command(3,NULL,0));
    const uint8_t expected[20]={0,0,1,0,8,0,12,0,0,0,0,0,3,0,0,0,0,0,0,0};
    assert(!memcmp(sent,expected,sizeof expected));assert(write_address==0x1f600 && s_sdio_tx_bufs==1);
    write_ok=false;assert(!ng_send_command(3,NULL,0));assert(s_sdio_tx_bufs==1);write_ok=true;
    s_ng.capabilities|=0x80;assert(ng_send_command(3,NULL,0));
    assert(hosted_wire_u16(sent+8)==24); /* 1+8+12+3 */
    uint8_t response[26]={0,0,2,0,14,0,12,0,0,0,0,0,3,2,0,0,0,0,0,0,2,3,4,5,6,7};
    uint16_t sum=compute_checksum(response,sizeof response);response[8]=(uint8_t)sum;response[9]=sum>>8;
    s_ng.pending=3;ng_receive_frames(response,sizeof response);
    assert(s_ng.reply && s_ng.response_status==2 && s_ng.mac[0]==2 && s_ng.mac[5]==7);
    s_ng.reply=false;response[8]^=1;ng_receive_frames(response,sizeof response);assert(!s_ng.reply);
    response[8]^=1;ng_receive_frames(response,25);assert(!s_ng.reply);
    response[12]=4;sum=compute_checksum(response,sizeof response)-response[8]-response[9];
    response[8]=(uint8_t)sum;response[9]=sum>>8;
    ng_receive_frames(response,sizeof response);assert(!s_ng.reply);
    assert(!ng_ota_version_supported()); /* NG 1.0.3.0.0 predates OTA. */
    s_ng.version[4]=1; assert(ng_ota_version_supported());
    uint8_t chunk[1018];memset(chunk,0xa5,sizeof chunk);chunk[0]=0xf8;chunk[1]=3;
    assert(ng_send_command(30,chunk,sizeof chunk));
    assert(write_length==1536 && write_address==0x1f800-1536);
    assert(hosted_wire_u16(sent+4)==1026 && sent[12]==30);
    assert(!memcmp(sent+20,chunk,sizeof chunk));
    assert(!ng_send_command(30,chunk,1019));
    puts("PASS: NG metadata, read-only command framing/checksum, failed send, response validation");
    return 0;
}
