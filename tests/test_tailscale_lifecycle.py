"""Compile actual lifecycle/control functions with deterministic network/task mocks.
Usage: python tests/test_tailscale_lifecycle.py --cc PATH_TO_ZIG
No live account, stored keys or authentication URLs are used.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'user/tailscale/tailscale_app.c'

def function(source,name):
    clean=re.sub(r'"(?:\\.|[^"\\])*"|/\*.*?\*/|//[^\n]*',lambda m:' '*len(m.group()),source,flags=re.S)
    hit=re.search(r'(?:static\s+)?(?:bool|void)\s+'+name+r'\s*\([^;{}]*\)\s*\{',clean)
    if not hit:raise ValueError(name)
    start=clean.index('{',hit.start());depth=1;end=start+1
    while depth:
        depth+=(clean[end]=='{')-(clean[end]=='}');end+=1
    return source[hit.start():end]

PREAMBLE=r'''
#ifdef NDEBUG
#error Regression assertions must be enabled
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define WIFI_STATE_CONNECTED 1
#define TS_STATE_STOPPED 0
#define TS_STATE_CONNECTING 1
#define TS_STATE_NEEDS_LOGIN 2
#define TS_STATE_RUNNING 3
#define TS_WG_PORT 41641
#define TS_SOCK_CTRL 3
#define TASK_PRIO_DEFAULT 1
#define mem_copy memcpy
#define ksnprintf snprintf
#define str_find strstr
#define kprintf(...) record_log()
static volatile bool s_want_up,s_daemon_running;
static volatile uint32_t s_control_generation;
static int s_state,s_peer_count;
static char s_auth_url[160],s_last_msg[80],s_saved_authkey[96],s_hostname[32]="test";
static uint8_t s_node_pub[32],s_disco_pub[32],s_my_ip[4];
typedef struct {struct {bool valid,online,wg_established;uint8_t endpoint_ip[4];}info;bool wg_hs_pending;} ts_peer_slot_t;
static ts_peer_slot_t s_peers[2];
typedef struct {int state;uint8_t ip[4];}wifi_status_t;
typedef struct {int sock;uint32_t generation;size_t pre_pos,pre_len;uint8_t pre_rx[256];}ts_ctrl_conn_t;
static int mode,notices,late_notices,allocs,workers,spawn_fail,callbacks,map_parses,network_posts;
static uint64_t now;
static void tailscale_down(void);
static bool ctrl_active(const ts_ctrl_conn_t *c);
static void ts_ensure_lock(void){} static void ts_lock(void){} static void ts_unlock(void){}
static void ts_load_or_gen_keys(void){} static void ts_save_keys(void){}
static void ts_set_msg(const char*s){snprintf(s_last_msg,sizeof s_last_msg,"%s",s);}
static void str_copy(char*d,size_t n,const char*s){snprintf(d,n,"%s",s);}
static bool str_eq(const char*a,const char*b){return strcmp(a,b)==0;}
static uint64_t hal_time_now_us(void){return now+=1000;}
static void task_sleep(int ms){now+=(uint64_t)ms*1000;}
static int task_create_stack(const char*n,void(*f)(void*),void*a,int p,size_t z){(void)n;(void)f;(void)a;(void)p;(void)z;workers++;return spawn_fail?-1:1;}
static void tailscale_daemon_task(void*a){(void)a;}
static void wifi_get_network_status(wifi_status_t*w){w->state=1;w->ip[0]=10;w->ip[1]=0;w->ip[2]=0;w->ip[3]=2;}
static void wifi_tcp_close_sock(int s){(void)s;}
static void*kmalloc_psram(size_t n){allocs++;return malloc(n);}static void*kmalloc(size_t n){allocs++;return malloc(n);}static void kfree(void*p){assert(p);allocs--;free(p);}
static void bytes_to_hex(const uint8_t*b,size_t n,char*out){(void)b;memset(out,'0',n*2);out[n*2]=0;}
static void record_log(void){notices++;if(!s_want_up)late_notices++;}
static void json_extract_string(const char*a,const char*b,const char*k,char*out,size_t n){(void)b;(void)k;str_copy(out,n,strstr(a,"AuthURL")?"https://example.invalid/authorize":"");}
static uint32_t get_le32(const uint8_t*p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static bool parse_map_response(const char*a,size_t n){(void)a;(void)n;map_parses++;s_my_ip[0]=100;return true;}
static void ts_udp_rx_callback(void){}static void ts_tun_tx_callback(void){}static void ts_tun_resolve_callback(void){}
static void wifi_set_udp_rx_callback(int port,void(*f)(void)){(void)port;if(f)callbacks++;}
static void wifi_set_tun_callbacks(uint8_t*ip,void(*a)(void),void(*b)(void)){(void)ip;(void)b;if(a)callbacks++;}
static void disco_send_ping_locked(ts_peer_slot_t*p){(void)p;}
static bool ctrl_connect_and_handshake(ts_ctrl_conn_t*c,uint32_t g){c->sock=3;c->generation=g;return ctrl_active(c);}
static bool ctrl_h2_post(ts_ctrl_conn_t*c,uint32_t stream,const char*path,const char*body,uint8_t*out,size_t cap,size_t*len,bool stop){
 (void)path;(void)body;(void)cap;(void)stop;if(!ctrl_active(c))return false;network_posts++;
 if(stream==1){strcpy((char*)out,"{\"AuthURL\":\"fixture\",\"MachineAuthorized\":false}");*len=strlen((char*)out);if(mode==1)tailscale_down();}
 else if(stream==3){strcpy((char*)out,"{\"MachineAuthorized\":true}");*len=strlen((char*)out);if(mode==2)tailscale_down();if(mode==4)return false;}
 else {out[0]=4;out[1]=out[2]=out[3]=0;memcpy(out+4,"map!",4);*len=8;if(mode==3)tailscale_down();}
 return true;
}
static int wifi_tcp_recv_sock(int sock,uint8_t*out,size_t n,int wait){(void)sock;(void)wait;memset(out,0,n);tailscale_down();return (int)n;}
'''
MAIN=r'''
static void reset(void){assert(allocs==0);s_want_up=false;s_daemon_running=false;s_control_generation=0;s_state=0;s_peer_count=0;s_auth_url[0]=0;mode=notices=late_notices=workers=spawn_fail=callbacks=map_parses=network_posts=0;now=0;}
static void begin(void){s_want_up=true;s_control_generation++;s_state=TS_STATE_CONNECTING;}
int main(void){
 reset();tailscale_boot_start();tailscale_boot_start();assert(!s_want_up&&!s_daemon_running&&workers==0&&callbacks==0);
 assert(!ts_sync_control_plane()&&network_posts==0&&allocs==0);
 for(int m=1;m<=3;m++){reset();begin();mode=m;assert(!ts_sync_control_plane());assert(!s_want_up&&s_state==TS_STATE_STOPPED&&s_auth_url[0]==0&&callbacks==0&&map_parses==0&&late_notices==0&&allocs==0);if(m==1)assert(notices==0);}
 reset();begin();mode=4;assert(!ts_sync_control_plane());assert(!ts_sync_control_plane());assert(notices==1&&allocs==0);tailscale_down();
 reset();begin();assert(ts_sync_control_plane());assert(s_state==TS_STATE_RUNNING&&map_parses==1&&callbacks==2&&allocs==0);tailscale_boot_start();assert(s_want_up&&s_state==TS_STATE_RUNNING);tailscale_down();
 reset();begin();ts_ctrl_conn_t old={.generation=s_control_generation};assert(ctrl_active(&old));tailscale_down();assert(!ctrl_active(&old));begin();assert(!ctrl_active(&old));
 ts_ctrl_conn_t current={.generation=s_control_generation};uint8_t byte;assert(!ctrl_raw_recv_exact(&current,&byte,1,1000));assert(!s_want_up);
 reset();spawn_fail=1;assert(!tailscale_up(NULL));assert(!s_want_up&&!s_daemon_running&&s_state==TS_STATE_STOPPED);
 puts("PASS: boot down; stopped sync; cancel registration/followup/map; URL dedup; successful map; shell preserves explicit up; generation invalidation; cancel read; spawn failure");return 0;
}
'''
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--cc',required=True);args=p.parse_args()
    source=SOURCE.read_text(encoding='utf-8-sig')
    names=('ctrl_active','ctrl_raw_recv_exact','tailscale_down','ts_sync_control_plane','tailscale_boot_start','tailscale_up')
    # Public signatures keep external linkage; the forward declaration matches.
    pre=PREAMBLE.replace('static void tailscale_down(void);','void tailscale_down(void);')
    with tempfile.TemporaryDirectory(prefix='tailscale-lifecycle-') as temp:
        c=Path(temp)/'test.c';exe=Path(temp)/'test.exe'
        c.write_text(pre+'\n'+'\n'.join(function(source,n) for n in names)+'\n'+MAIN)
        subprocess.run([args.cc,'cc','-std=c11','-O1','-UNDEBUG',str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
