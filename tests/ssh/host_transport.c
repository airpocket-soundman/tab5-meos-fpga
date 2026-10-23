#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <m5os/wifi.h>
#include <m5os/kprintf.h>
#include <m5os/vfs.h>
#include <m5os/kmalloc.h>
#include <m5os/task.h>
#include <m5os/hal_time.h>
#include <m5os/errno.h>
#define M5OS_BOARD_TAB5 1
static SOCKET host_sock;
static uint8_t *test_rx;
static size_t test_rx_len;
static unsigned test_rx_calls;
static char logbuf[100000]; static unsigned logend;
uint64_t hal_time_now_us(void){return GetTickCount64()*1000;}
void task_sleep(uint32_t n){Sleep(n);}
void kputs(const char*s){fputs(s,stderr);}
void kprintf(const char*fmt,...){(void)fmt;}
void kconsole_write(const char*s,size_t n){fwrite(s,1,n,stderr);}
uint32_t kconsole_log_end(void){return logend;}
size_t kconsole_log_read(uint32_t *p,char*b,size_t n){if(n>logend-*p)n=logend-*p;memcpy(b,logbuf+*p,n);*p+=n;return n;}
size_t kconsole_inject_n(const char*s,size_t n){memcpy(logbuf+logend,s,n);logend+=n;return n;}
void kconsole_inject(const char*s){kconsole_inject_n(s,strlen(s));}
int wifi_tcp_send_sock(int sock,const void*data,size_t n,uint32_t t){(void)sock;(void)t;return send(host_sock,data,n,0);}
int wifi_tcp_recv_sock(int sock,void*b,size_t n,uint32_t t){
if(test_rx){test_rx_calls++;if(!test_rx_len)return -1;if(n>test_rx_len)n=test_rx_len;memcpy(b,test_rx,n);test_rx+=n;test_rx_len-=n;return n;}
(void)sock;fd_set f;FD_ZERO(&f);FD_SET(host_sock,&f);struct timeval tv={t/1000,(t%1000)*1000};int r=select(0,&f,NULL,NULL,&tv);if(r<=0)return r; r=recv(host_sock,b,n,0);return r>0?r:-1;}
static bool ssh_rng_init(void){return true;}
static void ssh_random_bytes(uint8_t*p,size_t n){for(size_t i=0;i<n;i++)p[i]=rand();}


/* PRODUCTION_CODE */
void wifi_tcp_close_sock(int n){(void)n;}
int task_create_stack(const char*n,void(*f)(void*),void*a,uint8_t p,size_t s){return -1;}
int vfs_open(const char*p,uint32_t f){return ERR_NOENT;}
int vfs_read(int f,void*b,size_t n){return -1;}
int vfs_write(int f,const void*b,size_t n){return -1;}
int vfs_close(int f){return 0;}
void *kmalloc(size_t n){return malloc(n);}
void *kmalloc_psram(size_t n){return malloc(n);}
void kfree(void*p){free(p);}
void wifi_get_network_status(wifi_status_t*s){memset(s,0,sizeof(*s));}
bool wifi_tcp_listen_sock(int s,uint16_t p){return false;}
bool wifi_tcp_accept_sock(int s,uint32_t t){return false;}

static int bounds_test(void)
{
    _Static_assert(sizeof(size_t) == 4, "Boundary regression must use 32-bit size_t");
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
    uint8_t data[16] = {255,255,255,255};
    size_t pos = 0, string_n;
    const uint8_t *string;
    uint32_t value;
    CHECK(!buf_get_str(data, sizeof(data), &pos, &string, &string_n));
    pos = UINT32_MAX - 2;
    CHECK(!buf_get_u32(data, sizeof(data), &pos, &value));
    pos = sizeof(data) + 1;
    CHECK(!buf_get_str(data, sizeof(data), &pos, &string, &string_n));
    for (unsigned encrypted = 0; encrypted < 2; encrypted++) {
        const uint32_t lengths[] = {0xfffffffcu, 0xffffffffu, SSH_MAX_PKT - 3u};
        for (size_t i = 0; i < sizeof(lengths)/sizeof(lengths[0]); i++) {
            ssh_session_t *session = calloc(1,sizeof(*session));
            memset(data,0,sizeof(data)); put_u32_be(data,lengths[i]); data[4]=4;
            session->encrypted=encrypted;
            if (encrypted) {
                uint8_t zero[16]={0}; aes128_ctr_ctx_t enc;
                aes128_init(&enc,zero,zero); aes128_ctr_crypt(&enc,data,sizeof(data));
                aes128_init(&session->dec_s2c,zero,zero);
            }
            test_rx=data; test_rx_len=encrypted?16:4; test_rx_calls=0;
            uint8_t *payload;
            CHECK(ssh_recv_packet(session,&payload,10)<0);
            /* No payload/MAC read is attempted after the invalid header. */
            CHECK(test_rx_calls==1);
            free(session);
        }
    }
    test_rx=NULL;
    puts("PASS 32-bit string and clear/encrypted packet bounds");
    return 0;
#undef CHECK
}
int main(int argc,char**argv){
if(argc==2 && !strcmp(argv[1],"--bounds"))return bounds_test();
WSADATA w;WSAStartup(MAKEWORD(2,2),&w);SOCKET l=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(atoi(argv[1]));
if(bind(l,(struct sockaddr*)&a,sizeof(a))||listen(l,1))return 2;
fputs("READY\n",stdout);fflush(stdout);host_sock=accept(l,NULL,NULL);
strcpy(s_sshd_user,"tester");sha256_ctx_t h;sha256_init(&h);sha256_update(&h,"testpass123",11);sha256_final(&h,s_sshd_password_hash);
for(int i=0;i<32;i++)s_sshd_host_seed[i]=i;
ed25519_seed_to_pub(s_sshd_host_seed,s_sshd_host_pub);s_sshd_run=true;
ssh_session_t*s=calloc(1,sizeof(*s));s->sock=3;
bool ok=sshd_handshake(s);fprintf(stderr,"HANDSHAKE %d\n",ok);
if(ok){ok=sshd_authenticate(s);fprintf(stderr,"AUTH %d\n",ok);}
if(ok)sshd_console(s);
closesocket(host_sock);closesocket(l);WSACleanup();return ok?0:1;}
