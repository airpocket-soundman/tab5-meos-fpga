/* Fault-injected real application callbacks against upstream NimBLE headers.
 * This verifies policy/sequencing, not the cryptographic implementation. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <m5os/arch.h>
#include <m5os/task.h>
#include "os/os_mbuf.h"
#include "host/ble_hs.h"
#include "../../drivers/bluetooth/bluetooth.h"
static void meos_nimble_transport_configure(bt_hci_send_fn send,void *arg);
static bool meos_nimble_receive(uint8_t type,const uint8_t *p,size_t n);
#include "../../drivers/bluetooth/npl/nimble_app.inc"
struct ble_hs_cfg ble_hs_cfg;
static struct ble_npl_eventq queue;
static struct ble_npl_event *queued;
static unsigned connects,security_requests,writes,keys,clears,terminations,injections;
static unsigned scheduled_resets;
static bool available=true,broken_random,reenter_enable,fail_task;
static unsigned releases,initializations;
static uint16_t write_attribute;
static struct ble_gap_conn_desc peer;
static ble_gatt_disc_svc_fn *service_callback;
static ble_gatt_chr_fn *characteristic_callback;
static ble_gatt_dsc_fn *descriptor_callback;
static ble_gatt_attr_fn *write_callback;
static unsigned irq;
uint32_t arch_irq_disable(void) {uint32_t old=irq;irq=1;return old;}
void arch_irq_restore(uint32_t f) {irq=f;}
void nimble_port_init(void) {memset(&queue,0,sizeof queue);initializations++;}
void nimble_port_run(void) {}
struct ble_npl_eventq *nimble_port_get_dflt_eventq(void) {return &queue;}
void ble_npl_event_init(struct ble_npl_event *e,ble_npl_event_fn *fn,void *arg) {e->fn=fn;e->arg=arg;}
void ble_npl_eventq_put(struct ble_npl_eventq *q,struct ble_npl_event *e) {(void)q;queued=e;}
int task_create_stack(const char *n,void (*entry)(void *),void *a,uint8_t p,size_t s) {
 (void)n;(void)entry;(void)a;(void)p;assert(s>=8192);return fail_task?-1:1;
}
static void meos_nimble_transport_configure(bt_hci_send_fn send,void *arg) {(void)send;(void)arg;}
static bool meos_nimble_receive(uint8_t t,const uint8_t *p,size_t n) {(void)t;(void)p;return n>0;}
static bool transmit(void *a,uint8_t t,const uint8_t *p,size_t n) {(void)a;(void)t;(void)p;(void)n;return true;}
int ble_hs_util_ensure_addr(int random) {(void)random;return 0;}
int ble_hs_synced(void) {return 1;}
void ble_hs_sched_reset(int reason) {assert(reason==BLE_HS_ECONTROLLER);scheduled_resets++;}
bool hosted_hci_available(void) {return available;}
bool hosted_hci_claim(hosted_hci_receive_fn rx,hosted_hci_lost_fn lost,void *arg) {
 (void)rx;(void)lost;(void)arg;
 if(reenter_enable) {reenter_enable=false;assert(!bt_host_enable(NULL,NULL,NULL));}
 return true;
}
void hosted_hci_release(void *arg) {(void)arg;releases++;}
bool hosted_hci_send(uint8_t type,const uint8_t *p,size_t n) {(void)type;(void)p;(void)n;return true;}
int ble_hs_hci_rand(void *out,int length) {memset(out,broken_random?0xff:0x12,(size_t)length);return 0;}
uint16_t ble_uuid_u16(const ble_uuid_t *u) {return u->type==BLE_UUID_TYPE_16?((const ble_uuid16_t *)u)->value:0;}
int ble_hs_adv_parse_fields(struct ble_hs_adv_fields *f,const uint8_t *p,uint8_t n) {(void)f;(void)p;(void)n;return BLE_HS_EBADDATA;}
int ble_gap_disc(uint8_t own,int32_t duration,const struct ble_gap_disc_params *p,ble_gap_event_fn *cb,void *arg) {
 (void)own;(void)cb;(void)arg;assert(duration>0&&p->passive);return 0;
}
int ble_gap_connect(uint8_t own,const ble_addr_t *address,int32_t duration,const struct ble_gap_conn_params *p,ble_gap_event_fn *cb,void *arg) {
 (void)own;(void)duration;(void)p;(void)cb;(void)arg;peer.peer_ota_addr=*address;connects++;return 0;
}
int ble_gap_security_initiate(uint16_t connection) {assert(connection==7);security_requests++;return 0;}
int ble_gap_conn_find(uint16_t connection,struct ble_gap_conn_desc *out) {assert(connection==7);*out=peer;return 0;}
int ble_gap_terminate(uint16_t connection,uint8_t reason) {(void)connection;(void)reason;terminations++;return 0;}
int ble_gap_conn_cancel(void) {return 0;}
int ble_gap_disc_cancel(void) {return 0;}
int ble_sm_inject_io(uint16_t connection,struct ble_sm_io *io) {assert(connection==7&&io->action);injections++;return 0;}
int ble_store_ram_read(int type,const union ble_store_key *key,union ble_store_value *v) {(void)type;(void)key;(void)v;return BLE_HS_ENOENT;}
int ble_store_ram_write(int type,const union ble_store_value *v) {(void)type;(void)v;return 0;}
int ble_store_ram_delete(int type,const union ble_store_key *key) {(void)type;(void)key;return 0;}
int ble_store_clear(void) {clears++;return 0;}
int ble_gattc_disc_svc_by_uuid(uint16_t c,const ble_uuid_t *u,ble_gatt_disc_svc_fn *cb,void *arg) {
 (void)c;(void)arg;assert(ble_uuid_u16(u)==0x1812);service_callback=cb;return 0;
}
int ble_gattc_disc_all_chrs(uint16_t c,uint16_t start,uint16_t end,ble_gatt_chr_fn *cb,void *arg) {
 (void)c;(void)arg;assert(start==1&&end==30);characteristic_callback=cb;return 0;
}
int ble_gattc_disc_all_dscs(uint16_t c,uint16_t start,uint16_t end,ble_gatt_dsc_fn *cb,void *arg) {
 (void)c;(void)arg;assert(start==5&&end==30);descriptor_callback=cb;return 0;
}
int ble_gattc_write_no_rsp_flat(uint16_t c,uint16_t attr,const void *data,uint16_t n) {
 (void)c;assert(attr==3&&n==1&&*(const uint8_t *)data==0);writes++;return 0;
}
int ble_gattc_write_flat(uint16_t c,uint16_t attr,const void *data,uint16_t n,ble_gatt_attr_fn *cb,void *arg) {
 (void)c;(void)arg;assert(n==2&&*(const uint8_t *)data==1);write_attribute=attr;write_callback=cb;writes++;return 0;
}
int os_mbuf_copydata(const struct os_mbuf *m,int offset,int length,void *dst) {memcpy(dst,m->om_data+offset,(size_t)length);return 0;}
static void key(void *arg,uint8_t usage,bool pressed) {(void)arg;(void)usage;(void)pressed;keys++;}
static void run_command(void) {assert(queued);struct ble_npl_event *e=queued;queued=NULL;e->fn(e);}
static void disconnect(void) {
 struct ble_gap_event e={.type=BLE_GAP_EVENT_DISCONNECT};e.disconnect.conn.conn_handle=7;host_gap(&e,NULL);
}
static void connect(bool justworks) {
 uint8_t address[6]={1,2,3,4,5,6};assert(bt_host_connect(address,0,justworks));
 uint8_t other[6]={6,5,4,3,2,1};assert(!bt_host_connect(other,0,true));
 run_command();assert(!memcmp(peer.peer_ota_addr.val,address,6));
 struct ble_gap_event e={.type=BLE_GAP_EVENT_CONNECT};e.connect.conn_handle=7;host_gap(&e,NULL);
 assert(bt_app.status.state==BT_HOST_PAIRING);
}
static void encryption(bool authenticated,unsigned bytes) {
 peer.sec_state.encrypted=1;peer.sec_state.authenticated=authenticated;peer.sec_state.key_size=bytes;
 struct ble_gap_event e={.type=BLE_GAP_EVENT_ENC_CHANGE};e.enc_change.conn_handle=7;host_gap(&e,NULL);
}
static void discover_keyboard(bool success) {
 unsigned previous=writes;
 struct ble_gatt_error ok={0},done={.status=BLE_HS_EDONE};
 struct ble_gatt_svc service={.start_handle=1,.end_handle=30};
 assert(service_callback(7,&ok,&service,NULL)==0);service_callback(7,&done,NULL,NULL);
 struct ble_gatt_chr p={.def_handle=2,.val_handle=3,.properties=4,.uuid.u16=BLE_UUID16_INIT(0x2a4e)};
 struct ble_gatt_chr k={.def_handle=4,.val_handle=5,.properties=16,.uuid.u16=BLE_UUID16_INIT(0x2a22)};
 characteristic_callback(7,&ok,&p,NULL);characteristic_callback(7,&ok,&k,NULL);characteristic_callback(7,&done,NULL,NULL);
 struct ble_gatt_dsc d={.handle=6,.uuid.u16=BLE_UUID16_INIT(0x2902)};
 descriptor_callback(7,&ok,5,&d,NULL);descriptor_callback(7,&done,5,NULL,NULL);
 assert(writes==previous+2&&write_attribute==6&&!bt_app.hogp.input.authenticated);
 struct ble_gatt_error rejected={.status=BLE_HS_EAUTHEN};
 write_callback(7,success?&ok:&rejected,NULL,NULL);
 assert(bt_app.status.state==(success?BT_HOST_INPUT:BT_HOST_ERROR));
}
int main(void) {
 available=false;assert(!bt_host_enable(key,NULL,NULL));available=true;
 assert(bt_host_start(transmit,NULL,key,NULL,NULL));host_sync();assert(bt_app.status.state==BT_HOST_READY);
 assert(bt_host_scan(100));run_command();assert(connects==0);
 struct ble_gap_event finished={.type=BLE_GAP_EVENT_DISC_COMPLETE};host_gap(&finished,NULL);
 assert(bt_host_forget_bonds());run_command();assert(clears==1);
 connect(false);encryption(false,16);assert(bt_app.status.state==BT_HOST_ERROR&&writes==0);disconnect();
 connect(true);encryption(false,7);assert(bt_app.status.state==BT_HOST_ERROR&&writes==0);disconnect();
 connect(false);
 struct ble_gap_event prompt={.type=BLE_GAP_EVENT_PASSKEY_ACTION};prompt.passkey.conn_handle=7;prompt.passkey.params.action=BLE_SM_IOACT_NUMCMP;prompt.passkey.params.numcmp=123456;
 host_gap(&prompt,NULL);assert(injections==0);
 assert(!bt_host_pair_reply(true,0)); /* Approval must name an observed generation. */
 uint32_t previous_challenge=bt_app.status.pairing_generation;
 assert(bt_host_pair_reply_for(previous_challenge,true,0));
 /* A new numeric comparison replaces queued consent before dispatch. */
 prompt.passkey.params.numcmp=654321;host_gap(&prompt,NULL);
 run_command();assert(injections==0&&bt_app.status.pairing_action==BLE_SM_IOACT_NUMCMP);
 assert(!bt_host_pair_reply_for(previous_challenge,true,0));
 assert(bt_host_pair_reply_for(bt_app.status.pairing_generation,true,0));run_command();assert(injections==1);
 encryption(true,16);assert(bt_app.status.state==BT_HOST_DISCOVERING);discover_keyboard(true);
 uint8_t report[8]={0,0,4};assert(bt_hogp_notify(&bt_app.hogp,7,5,report,8)&&keys==1);
 bt_host_transport_lost();run_command();assert(keys==2&&!bt_app.hogp.connected&&bt_app.status.state==BT_HOST_ERROR);
 assert(!bt_host_scan(100));assert(security_requests==3);
 reenter_enable=true;assert(bt_host_enable(key,NULL,NULL));assert(!reenter_enable);
 run_command();assert(scheduled_resets==1&&bt_app.status.state==BT_HOST_STARTING);
 host_sync();connect(true);encryption(false,16);assert(bt_app.status.state==BT_HOST_DISCOVERING);
 discover_keyboard(false);assert(!bt_app.hogp.input.authenticated);disconnect();
 connect(false);prompt.passkey.params.action=BLE_SM_IOACT_DISP;broken_random=true;
 host_gap(&prompt,NULL);assert(bt_app.status.state==BT_HOST_ERROR&&!bt_app.status.pairing_action);
 assert(!bt_host_pair_reply(true,0));
 /* A failed initial task allocation must not retain/reacquire a dead owner or
  * construct upstream state a second time. Fresh mock instance only. */
 memset(&bt_app,0,sizeof bt_app);fail_task=true;
 unsigned old_init=initializations,old_release=releases;
 assert(!bt_host_enable(key,NULL,NULL));assert(initializations==old_init+1&&releases==old_release+1);
 assert(!bt_host_enable(key,NULL,NULL));assert(initializations==old_init+1&&releases==old_release+2);
 puts("NimBLE application policy/GATT fault traces PASS");
}
