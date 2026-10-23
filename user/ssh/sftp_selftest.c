/* Local actual-engine SFTP storage test; transport and SSH authentication excluded. */
#include "sftp_selftest.h"
#include "sftp_server.h"
#include <m5os/hal_time.h>
#include <m5os/kmalloc.h>
#include <m5os/kprintf.h>
#include <string.h>
#define ST_BYTES 8192u
#define ST_TX (ST_BYTES+256u)
#define ST_RX (SFTP_SERVER_READ_MAX+512u)
typedef struct {
    sftp_server_t *server;
    uint8_t tx[ST_TX],rx[ST_RX],handle[64];
    size_t tx_n,rx_n,handle_n;
    uint32_t serial;
    char path[96];
} st_state_t;
static uint32_t st_load(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void st_word(st_state_t *s,uint32_t n){s->tx[s->tx_n++]=n>>24;s->tx[s->tx_n++]=n>>16;s->tx[s->tx_n++]=n>>8;s->tx[s->tx_n++]=n;}
static void st_string(st_state_t *s,const void *p,size_t n){st_word(s,(uint32_t)n);memcpy(s->tx+s->tx_n,p,n);s->tx_n+=n;}
static void st_begin(st_state_t *s,uint8_t kind){s->tx_n=4;s->tx[s->tx_n++]=kind;if(kind!=1)st_word(s,++s->serial);}
static bool st_exchange(st_state_t *s){
    size_t body=s->tx_n-4;s->tx[0]=body>>24;s->tx[1]=body>>16;s->tx[2]=body>>8;s->tx[3]=body;
    size_t at=0;
    while(at<s->tx_n){size_t n=s->tx_n-at;if(n>37)n=37;size_t used=sftp_server_feed(s->server,s->tx+at,n);if(!used || used>n)return false;at+=used;}
    s->rx_n=0;
    for(;;){
        size_t capacity=sizeof s->rx-s->rx_n;if(capacity>43)capacity=43;
        if(!capacity)return false;
        size_t n=sftp_server_read(s->server,s->rx+s->rx_n,capacity);
        if(n>capacity)return false;
        if(!n)break;
        s->rx_n+=n;
    }
    return !sftp_server_failed(s->server) && s->rx_n>=5 && st_load(s->rx)==s->rx_n-4 &&
        (s->rx[4]==2 || (s->rx_n>=9 && st_load(s->rx+5)==s->serial));
}
static bool st_status(st_state_t *s,uint32_t expected){
    if(s->rx_n<21 || s->rx[4]!=101 || st_load(s->rx+9)!=expected)return false;
    size_t at=13;
    for(unsigned i=0;i<2;i++){
        if(at>s->rx_n || s->rx_n-at<4)return false;
        uint32_t n=st_load(s->rx+at);at+=4;
        if(n>s->rx_n-at)return false;
        at+=n;
    }
    return at==s->rx_n;
}
static bool st_open(st_state_t *s,uint32_t flags){
    st_begin(s,3);st_string(s,s->path,strlen(s->path));st_word(s,flags);st_word(s,0);
    if(!st_exchange(s) || s->rx_n<13 || s->rx[4]!=102)return false;
    uint32_t n=st_load(s->rx+9);
    if(!n || n>sizeof s->handle || s->rx_n!=13+n)return false;
    memcpy(s->handle,s->rx+13,n);s->handle_n=n;return true;
}
static bool st_close(st_state_t *s){st_begin(s,4);st_string(s,s->handle,s->handle_n);return st_exchange(s)&&st_status(s,0);}
static uint8_t st_pattern(unsigned i){return (uint8_t)((i*37u)^(i>>3)^(i>>8)^0xa5u);}
static bool st_check_contents(st_state_t *s){
    if(!st_open(s,1))return false;
    for(unsigned offset=0;offset<ST_BYTES;offset+=SFTP_SERVER_READ_MAX){
        st_begin(s,5);st_string(s,s->handle,s->handle_n);st_word(s,0);st_word(s,offset);st_word(s,SFTP_SERVER_READ_MAX);
        if(!st_exchange(s) || s->rx[4]!=103 || s->rx_n!=13+SFTP_SERVER_READ_MAX || st_load(s->rx+9)!=SFTP_SERVER_READ_MAX)return false;
        for(unsigned i=0;i<SFTP_SERVER_READ_MAX;i++)if(s->rx[13+i]!=st_pattern(offset+i))return false;
    }
    st_begin(s,5);st_string(s,s->handle,s->handle_n);st_word(s,0);st_word(s,ST_BYTES);st_word(s,1);
    if(!st_exchange(s)||!st_status(s,1))return false;
    return st_close(s);
}
bool sftp_storage_selftest(const char *root){
    if(!root || strcmp(root,"/sd")){kprintf("SFTP storage selftest FAIL: root must be /sd\n");return false;}
    st_state_t *s=kmalloc_psram(sizeof *s);
    if(!s){kprintf("SFTP storage selftest FAIL: allocation\n");return false;}
    memset(s,0,sizeof *s);s->server=sftp_server_create(true);
    const char *stage="create session";bool passed=false,created=false;
    if(!s->server)goto done;
    stage="INIT";st_begin(s,1);st_word(s,3);
    if(!st_exchange(s)||s->rx_n!=9||s->rx[4]!=2||st_load(s->rx+5)!=3)goto done;
    stage="exclusive create";
    uint32_t seed=(uint32_t)hal_time_now_us();
    for(unsigned i=0;i<8;i++){
        ksnprintf(s->path,sizeof s->path,"/sd/sftp_%08x.bin",seed+i);
        if(st_open(s,2|8|16|32)){created=true;break;}
        if(!st_status(s,4))goto done;
    }
    if(!created)goto done;
    stage="WRITE 8192";st_begin(s,6);st_string(s,s->handle,s->handle_n);st_word(s,0);st_word(s,0);st_word(s,ST_BYTES);
    for(unsigned i=0;i<ST_BYTES;i++)s->tx[s->tx_n++]=st_pattern(i);
    if(!st_exchange(s)||!st_status(s,0))goto done;
    stage="CLOSE write";if(!st_close(s))goto done;
    stage="READ verify";if(!st_check_contents(s))goto done;
    stage="existing EXCL refusal";
    if(st_open(s,2|8|16|32)||!st_status(s,4))goto done;
    stage="READ verify unchanged";if(!st_check_contents(s))goto done;
    passed=true;
 done:
    if(s->server && sftp_server_destroy(s->server)){passed=false;stage="destroy/close";}
    kprintf("SFTP storage selftest %s stage=%s fresh_file=%s bytes=%u\n",passed?"PASS":"FAIL",passed?"complete":stage,created?s->path:"-",created?ST_BYTES:0);
    kfree(s);return passed;
}
