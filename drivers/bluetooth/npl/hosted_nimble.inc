/* Original Hosted-HCI bridge for Apache NimBLE 1.8.0. Not compiled by default.
 * TX runs in the NimBLE task; SDIO callback must serialize/copy synchronously.
 * RX runs in the independent SDIO owner, allowing command semaphores to wake.
 * Never run this bridge and hci_probe on the same controller. */
#include "../bluetooth.h"
#include <string.h>
#include "os/os_mbuf.h"
#include "nimble/transport.h"
#include "host/ble_hs.h"
static bt_hci_send_fn meos_nimble_send;
static void *meos_nimble_send_arg;
static void meos_nimble_transport_configure(bt_hci_send_fn send,void *arg) {
    meos_nimble_send=send;meos_nimble_send_arg=arg;
}
void ble_transport_ll_init(void) { /* SDIO owner initializes the physical bus. */ }
int ble_transport_to_ll_cmd_impl(void *buffer) {
    uint8_t *p=buffer;
    bool sent=meos_nimble_send&&meos_nimble_send(meos_nimble_send_arg,1,p,(size_t)p[2]+3);
    ble_transport_free(buffer);return sent?0:BLE_HS_ECONTROLLER;
}
int ble_transport_to_ll_acl_impl(struct os_mbuf *om) {
    uint8_t packet[MYNEWT_VAL(BLE_TRANSPORT_ACL_SIZE)+4];
    size_t n=OS_MBUF_PKTLEN(om);bool sent=false;
    if(n>=4&&n<=sizeof packet&&os_mbuf_copydata(om,0,(int)n,packet)==0) {
        size_t declared=(size_t)packet[2]|((size_t)packet[3]<<8);
        if(declared+4==n&&meos_nimble_send)
            sent=meos_nimble_send(meos_nimble_send_arg,2,packet,n);
    }
    os_mbuf_free_chain(om);return sent?0:BLE_HS_ECONTROLLER;
}
int ble_transport_to_ll_iso_impl(struct os_mbuf *om) {
    os_mbuf_free_chain(om);return BLE_HS_ENOTSUP;
}
static bool meos_nimble_receive(uint8_t type,const uint8_t *p,size_t n) {
    if(!p)return false;
    if(type==4) {
        if(n<2||n!=(size_t)p[1]+2||n>MYNEWT_VAL(BLE_TRANSPORT_EVT_SIZE))return false;
        if((p[0]==0x0e&&n<5)||(p[0]==0x0f&&n!=6)||(p[0]==0x3e&&n<3))return false;
        bool discardable=p[0]==0x3e&&(p[2]==2||p[2]==13);
        void *event=ble_transport_alloc_evt(discardable);
        if(!event)return false;
        memcpy(event,p,n);
        /* NimBLE owns the buffer, including its own drop/error paths. */
        return ble_transport_to_hs_evt(event)==0;
    }
    if(type==2) {
        if(n<4||n>MYNEWT_VAL(BLE_TRANSPORT_ACL_SIZE)+4||n!=((size_t)p[2]|((size_t)p[3]<<8))+4)return false;
        struct os_mbuf *om=ble_transport_alloc_acl_from_ll();
        if(!om)return false;
        if(os_mbuf_append(om,p,(uint16_t)n)!=0) {os_mbuf_free_chain(om);return false;}
        return ble_transport_to_hs_acl(om)==0;
    }
    return false;
}
