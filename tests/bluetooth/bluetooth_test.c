#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../drivers/bluetooth/bluetooth.h"
static uint8_t sent[16];static size_t sent_n;static unsigned sends,adverts,keys,mice;
static bool transport_ok=true;
static bool send(void *a,uint8_t type,const uint8_t *p,size_t n) {
 (void)a;assert(type==1&&n<=sizeof sent);memcpy(sent,p,n);sent_n=n;sends++;return transport_ok;
}
static void adv(void *a,const bt_advertisement_t *d) {
 (void)a;assert(d->hid_service&&d->rssi==-40&&!strcmp(d->name,"K?B"));adverts++;
}
static void complete(bt_hci_t *h,uint16_t op,const uint8_t *extra,size_t n) {
 uint8_t e[20]={0x0e,(uint8_t)(4+n),1,(uint8_t)op,(uint8_t)(op>>8),0};
 if(n)memcpy(e+6,extra,n);assert(bt_hci_receive(h,4,e,n+6));
}
static void init(bt_hci_t *h) {
 assert(bt_hci_open(h,send,adv,NULL,0));assert(sent_n==3&&sent[0]==3&&sent[1]==12);
 complete(h,0x0c03,NULL,0);bt_hci_poll(h,1);
 const uint8_t ver[8]={12,0x34,0x12,12,0xe5,2,0,0};
 complete(h,0x1001,ver,8);bt_hci_poll(h,2);
 const uint8_t mac[6]={1,2,3,4,5,6};complete(h,0x1009,mac,6);bt_hci_poll(h,3);
 assert(sent_n==11&&sent[10]==0x20);complete(h,0x0c01,NULL,0);bt_hci_poll(h,4);
 assert(sent[3]==2);complete(h,0x2001,NULL,0);bt_hci_poll(h,5);
 assert(h->status.state==BT_HCI_READY&&h->status.revision==0x1234&&h->status.manufacturer==0x2e5);
}
static void scan_test(void) {
 bt_hci_t h;init(&h);assert(!bt_hci_scan(&h,60001,10));assert(bt_hci_scan(&h,100,10));
 assert(sent[0]==0x0b&&sent[3]==0);complete(&h,0x200b,NULL,0);bt_hci_poll(&h,11);
 assert(sent[0]==0x0c&&sent[3]==1);complete(&h,0x200c,NULL,0);
 uint8_t e[]={0x3e,21,2,1,0,0,1,2,3,4,5,6,9,3,3,0x12,0x18,4,9,'K',1,'B',0xd8};
 assert(sizeof e==23);assert(bt_hci_receive(&h,4,e,sizeof e));assert(adverts==1);
 /* Hostile nested AD length must not reach the callback. */
 e[13]=31;assert(!bt_hci_receive(&h,4,e,sizeof e));assert(adverts==1);
 bt_hci_poll(&h,110);assert(sent[3]==0);complete(&h,0x200c,NULL,0);
 assert(h.status.state==BT_HCI_READY);
 assert(bt_hci_scan(&h,100,120));complete(&h,0x200b,NULL,0);bt_hci_poll(&h,121);
 complete(&h,0x200c,NULL,0);bt_hci_scan_stop(&h);bt_hci_poll(&h,122);
 assert(sent[3]==0);complete(&h,0x200c,NULL,0);assert(h.status.state==BT_HCI_READY);
}
static void failure_test(void) {
 bt_hci_t h;assert(bt_hci_open(&h,send,NULL,NULL,0));
 uint8_t foreign[]={0x0e,4,1,9,0x10,0};assert(bt_hci_receive(&h,4,foreign,sizeof foreign));assert(h.waiting);
 unsigned before=sends;bt_hci_poll(&h,2000);assert(h.status.error==BT_HCI_ERROR_TIMEOUT&&sends==before);
 transport_ok=false;assert(!bt_hci_open(&h,send,NULL,NULL,0));assert(h.status.error==BT_HCI_ERROR_TRANSPORT);transport_ok=true;
 assert(bt_hci_open(&h,send,NULL,NULL,0));uint8_t reject[]={0x0e,4,1,3,12,1};
 assert(bt_hci_receive(&h,4,reject,sizeof reject));assert(h.status.error==BT_HCI_ERROR_CONTROLLER);
 bt_hci_transport_lost(&h);assert(h.status.state==BT_HCI_OFF);
 assert(bt_hci_open(&h,send,NULL,NULL,0));uint8_t no_credit[]={0x0e,4,0,3,12,0};
 assert(bt_hci_receive(&h,4,no_credit,sizeof no_credit));bt_hci_poll(&h,2000);assert(h.status.error==BT_HCI_ERROR_TIMEOUT);
}
static void key(void *arg,uint8_t usage,bool pressed) {(void)arg;(void)usage;(void)pressed;keys++;}
static void mouse(void *arg,uint8_t buttons,int16_t x,int16_t y,int16_t wheel) {
 (void)arg;assert(wheel==0);if(buttons){assert(x==-128&&y==127);}mice++;
}
static void hid_test(void) {
 bt_hid_boot_t h;bt_hid_boot_init(&h,key,mouse,NULL);uint8_t r[8]={2,0,4};
 assert(!bt_hid_boot_keyboard(&h,r,8)&&keys==0);bt_hid_boot_set_authenticated(&h,true);
 assert(bt_hid_boot_keyboard(&h,r,8)&&keys==2);assert(bt_hid_boot_keyboard(&h,r,8)&&keys==2);
 uint8_t rollover[8]={0,0,1,1,1,1,1,1};assert(!bt_hid_boot_keyboard(&h,rollover,8)&&keys==2);
 uint8_t duplicate[8]={0,0,4,4};assert(!bt_hid_boot_keyboard(&h,duplicate,8)&&keys==2);
 uint8_t m[3]={1,128,127};assert(bt_hid_boot_mouse(&h,m,3)&&mice==1);assert(!bt_hid_boot_mouse(&h,m,2));
 bt_hid_boot_set_authenticated(&h,false);assert(keys==4&&mice==2);
 assert(!bt_hid_boot_keyboard(&h,r,8));assert(!bt_hid_boot_mouse(&h,m,3));
}
static void hogp_test(void) {
 bt_hogp_t h;bt_hogp_init(&h,key,mouse,NULL);assert(bt_hogp_connect(&h,42));
 bt_hogp_handles_t g={1,30,3,5,6,8,9};
 uint8_t r[8]={0,0,4},v[2];uint16_t attr;size_t n;uint32_t token;
 unsigned previous=keys;
 assert(!bt_hogp_notify(&h,42,5,r,8));assert(!bt_hogp_discovered(&h,41,&g));
 assert(bt_hogp_discovered(&h,42,&g));
 assert(bt_hogp_next(&h,&attr,v,&n,&token)==BT_HOGP_NO_ACTION);
 bt_hogp_security(&h,42,true,true,7);assert(!h.secure);
 bt_hogp_security(&h,42,true,false,16);assert(!h.secure);
 bt_hogp_security(&h,42,true,true,16);
 assert(bt_hogp_next(&h,&attr,v,&n,&token)==BT_HOGP_WRITE_PROTOCOL&&attr==3&&v[0]==0&&n==1);
 uint32_t stale=token;bt_hogp_write_complete(&h,42,token+1,true);assert(!h.protocol_set);
 bt_hogp_write_complete(&h,42,token,true);assert(h.protocol_set);
 assert(!bt_hogp_notify(&h,42,5,r,8));
 assert(bt_hogp_next(&h,&attr,v,&n,&token)==BT_HOGP_SUBSCRIBE_KEYBOARD&&attr==6&&n==2&&v[0]==1&&v[1]==0);
 bt_hogp_write_complete(&h,42,token,true);
 assert(!bt_hogp_notify(&h,41,5,r,8));assert(!bt_hogp_notify(&h,42,6,r,8));
 assert(bt_hogp_notify(&h,42,5,r,8)&&keys==previous+1);
 assert(bt_hogp_next(&h,&attr,v,&n,&token)==BT_HOGP_SUBSCRIBE_MOUSE);
 bt_hogp_write_complete(&h,42,token,false);assert(!h.secure&&keys==previous+2);
 assert(!bt_hogp_notify(&h,42,5,r,8));
 bt_hogp_disconnect(&h,41);assert(h.connected);bt_hogp_disconnect(&h,42);assert(!h.connected);
 assert(bt_hogp_connect(&h,42));assert(bt_hogp_discovered(&h,42,&g));bt_hogp_security(&h,42,true,true,16);
 assert(bt_hogp_next(&h,&attr,v,&n,&token)==BT_HOGP_WRITE_PROTOCOL);
 bt_hogp_write_complete(&h,42,stale,true);assert(!h.protocol_set); /* Old connection callback cannot authorize new one. */
 g.mouse_ccc=g.keyboard_value;assert(!bt_hogp_discovered(&h,42,&g));
}
int main(void) {scan_test();failure_test();hid_test();hogp_test();puts("Bluetooth probe/HID safety tests PASS");}
