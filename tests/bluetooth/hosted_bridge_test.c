/* Real bridge exercised with pinned MCU C6->host H4 payload fixtures. */
#include <assert.h>
#include <stdio.h>
#include "../../drivers/bluetooth/npl/hosted_nimble.inc"
static uint8_t event_buffer[257],acl_buffer[255],sent[8];
static unsigned event_count,acl_count,released;
static struct os_mbuf acl;
static bool send(void *arg,uint8_t type,const uint8_t *p,size_t n) {
 (void)arg;assert(type==1&&n==3);memcpy(sent,p,n);return true;
}
void *ble_transport_alloc_evt(int discardable) {(void)discardable;return event_buffer;}
void ble_transport_free(void *buffer) {(void)buffer;released++;}
int ble_transport_to_hs_evt_impl(void *buffer) {assert(buffer==event_buffer);event_count++;return 0;}
struct os_mbuf *ble_transport_alloc_acl_from_ll(void) {acl.om_data=acl_buffer;return &acl;}
int os_mbuf_append(struct os_mbuf *om,const void *data,uint16_t length) {assert(om==&acl);memcpy(om->om_data,data,length);return 0;}
int ble_transport_to_hs_acl_impl(struct os_mbuf *om) {assert(om==&acl);acl_count++;return 0;}
int os_mbuf_free_chain(struct os_mbuf *om) {(void)om;return 0;}
int os_mbuf_copydata(const struct os_mbuf *om,int offset,int length,void *out) {memcpy(out,om->om_data+offset,(size_t)length);return 0;}
int main(void) {
 meos_nimble_transport_configure(send,NULL);
 /* Host->C6 excludes H4; SDIO transport writes this type in header byte11. */
 uint8_t reset[]={3,12,0};assert(!ble_transport_to_ll_cmd_impl(reset));
 assert(!memcmp(sent,reset,3)&&released==1);
 /* C6 slave copies the full VHCI frame, leaving header byte11 zero. */
 const uint8_t c6_reset[]={4,14,4,1,3,12,0};
 assert(!meos_nimble_receive(0,c6_reset,sizeof c6_reset)&&!event_count);
 assert(meos_nimble_receive(c6_reset[0],c6_reset+1,sizeof c6_reset-1));
 assert(event_count==1&&event_buffer[0]==14&&event_buffer[3]==3&&event_buffer[4]==12);
 const uint8_t c6_acl[]={2,1,0x20,3,0,0xaa,0xbb,0xcc};
 assert(meos_nimble_receive(c6_acl[0],c6_acl+1,sizeof c6_acl-1)&&acl_count==1);
 assert(!memcmp(acl_buffer,c6_acl+1,sizeof c6_acl-1));
 assert(!meos_nimble_receive(4,c6_reset+1,sizeof c6_reset-2));
 assert(!meos_nimble_receive(2,c6_acl+1,sizeof c6_acl-2));
 puts("Hosted MCU asymmetric H4/bridge fixtures PASS");
}
