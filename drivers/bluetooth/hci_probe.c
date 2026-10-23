/* Original MeOS polled BLE controller discovery. HCI command/event layouts follow
 * Bluetooth Core Vol 4 Part E; Hosted envelope uses Espressif's published interface.
 * No upstream stack implementation is copied into this module. */
#include "bluetooth.h"
#include <string.h>
#define HCI_COMMAND 1u
#define HCI_EVENT 4u
#define COMMAND_TIMEOUT 2000u
static uint16_t le16(const uint8_t *p) {return (uint16_t)p[0]|((uint16_t)p[1]<<8);}
static void fail(bt_hci_t *h,bt_hci_error_t e) {h->status.error=e;h->status.state=BT_HCI_FAILED;h->waiting=false;}
static bool command(bt_hci_t *h,uint16_t opcode,const uint8_t *parameters,size_t length,uint64_t now) {
    uint8_t packet[11];
    if(h->waiting||!h->credits||length>8) return false;
    packet[0]=(uint8_t)opcode;packet[1]=(uint8_t)(opcode>>8);packet[2]=(uint8_t)length;
    if(length)memcpy(packet+3,parameters,length);
    h->status.opcode=opcode;h->deadline=now+COMMAND_TIMEOUT;
    h->waiting=true;h->credits--;
    if(!h->send(h->arg,HCI_COMMAND,packet,length+3)) {fail(h,BT_HCI_ERROR_TRANSPORT);return false;}
    return true;
}
bool bt_hci_open(bt_hci_t *h,bt_hci_send_fn send,bt_hci_advertisement_fn adv,void *arg,uint64_t now) {
    if(!h||!send)return false;
    memset(h,0,sizeof *h);h->send=send;h->advertisement=adv;h->arg=arg;
    h->credits=1;h->status.state=BT_HCI_INITIALIZING;
    return command(h,0x0c03,NULL,0,now); /* HCI_Reset */
}
void bt_hci_transport_lost(bt_hci_t *h) {
    if(!h)return;
    h->waiting=false;h->credits=0;h->stop_requested=false;
    h->status.state=BT_HCI_OFF;h->status.error=BT_HCI_ERROR_TRANSPORT;
}
void bt_hci_poll(bt_hci_t *h,uint64_t now) {
    if(!h||h->status.state==BT_HCI_OFF||h->status.state==BT_HCI_FAILED)return;
    if(h->waiting||!h->credits) {if(now>=h->deadline)fail(h,BT_HCI_ERROR_TIMEOUT);return;}
    if(h->status.state==BT_HCI_INITIALIZING) {
        static const uint8_t events[8]={0,0,0,0,0,0,0,0x20}; /* LE Meta event bit 61. */
        static const uint8_t le_events[8]={2,0,0,0,0,0,0,0}; /* Legacy advertising report. */
        switch(h->phase) {
        case 1:command(h,0x1001,NULL,0,now);break; /* Read local version. */
        case 2:command(h,0x1009,NULL,0,now);break; /* Read public address. */
        case 3:command(h,0x0c01,events,sizeof events,now);break;
        case 4:command(h,0x2001,le_events,sizeof le_events,now);break;
        default:if(h->phase>=5)h->status.state=BT_HCI_READY;break;
        }
    } else if(h->status.state==BT_HCI_SCANNING) {
        if(h->phase==6) {
            const uint8_t enable[2]={1,1}; /* Enable; duplicate filtering. */
            command(h,0x200c,enable,2,now);
        } else if(h->phase==7&&(h->stop_requested||now>=h->scan_deadline)) {
            const uint8_t disable[2]={0,0};command(h,0x200c,disable,2,now);
        }
    }
}
bool bt_hci_scan(bt_hci_t *h,uint32_t duration,uint64_t now) {
    if(!h||h->status.state!=BT_HCI_READY||h->waiting||!duration||duration>60000)return false;
    /* Passive, 100ms interval, 50ms window, public own address, accept all. */
    const uint8_t params[7]={0,0xa0,0,0x50,0,0,0};
    h->phase=5;h->stop_requested=false;h->scan_deadline=now+duration;
    h->status.state=BT_HCI_SCANNING;
    return command(h,0x200b,params,sizeof params,now);
}
void bt_hci_scan_stop(bt_hci_t *h) {if(h)h->stop_requested=true;}
static bool parse_advertisements(bt_hci_t *h,const uint8_t *p,size_t n) {
    if(n<2||p[0]!=2)return false;
    size_t pos=2;
    /* Validate every nested boundary before delivering any report. */
    for(unsigned i=0;i<p[1];i++) {
        if(n-pos<10)return false;
        size_t bytes=p[pos+8];if(bytes>31||bytes+10>n-pos)return false;
        size_t at=0;const uint8_t *ad=p+pos+9;
        while(at<bytes) {size_t span=ad[at];if(!span)break;if(span+1>bytes-at)return false;at+=span+1;}
        pos+=bytes+10;
    }
    if(pos!=n)return false;
    pos=2;
    for(unsigned i=0;i<p[1];i++) {
        bt_advertisement_t a={0};a.event_type=p[pos];a.address_type=p[pos+1];
        memcpy(a.address,p+pos+2,6);size_t bytes=p[pos+8];const uint8_t *ad=p+pos+9;
        a.rssi=(int8_t)ad[bytes];
        for(size_t at=0;at<bytes;) {
            size_t span=ad[at];if(!span)break;
            uint8_t tag=ad[at+1];const uint8_t *v=ad+at+2;size_t len=span-1;
            if(tag==8||tag==9) {
                size_t take=len<sizeof(a.name)-1?len:sizeof(a.name)-1;
                for(size_t j=0;j<take;j++)a.name[j]=(v[j]<32||v[j]==127)?'?':(char)v[j];
                a.name[take]=0;
            }
            if(tag==2||tag==3)for(size_t j=0;j+1<len;j+=2)if(le16(v+j)==0x1812)a.hid_service=true;
            at+=span+1;
        }
        h->status.advertisements++;
        if(h->advertisement)h->advertisement(h->arg,&a);
        pos+=bytes+10;
    }
    return true;
}
bool bt_hci_receive(bt_hci_t *h,uint8_t type,const uint8_t *p,size_t n) {
    if(!h||!p||h->status.state==BT_HCI_OFF)return false;
    h->status.received++;
    if(type!=HCI_EVENT||n<2||(size_t)p[1]+2!=n)goto malformed;
    if(p[0]==0x10) {fail(h,BT_HCI_ERROR_CONTROLLER);return true;} /* Hardware error. */
    if(p[0]==0x3e) {
        if(h->status.state!=BT_HCI_SCANNING)return true;
        if(!parse_advertisements(h,p+2,n-2))goto malformed;
        return true;
    }
    if(p[0]!=0x0e&&p[0]!=0x0f)return true;
    if((p[0]==0x0e&&n<6)||(p[0]==0x0f&&n!=6))goto malformed;
    uint16_t opcode=p[0]==0x0e?le16(p+3):le16(p+4);
    uint8_t status=p[0]==0x0e?p[5]:p[2];
    h->credits=p[0]==0x0e?p[2]:p[3];
    if(!h->waiting||opcode!=h->status.opcode)return true;
    h->status.controller_status=status;
    if(status) {fail(h,BT_HCI_ERROR_CONTROLLER);return true;}
    if(p[0]==0x0f)return true; /* Commands here require Command Complete. */
    if(opcode==0x1001) {
        if(n!=14)goto malformed;
        h->status.hci_version=p[6];h->status.revision=le16(p+7);h->status.manufacturer=le16(p+10);
    } else if(opcode==0x1009) {
        if(n!=12)goto malformed;
        memcpy(h->status.address,p+6,6);
    } else if(n!=6)goto malformed;
    h->waiting=false;
    if(h->status.state==BT_HCI_INITIALIZING)h->phase++;
    else if(h->status.state==BT_HCI_SCANNING) {
        if(h->phase==5||h->phase==6)h->phase++;
        else if(h->phase==7) {h->status.state=BT_HCI_READY;h->stop_requested=false;}
    }
    return true;
malformed:
    h->status.malformed++;return false;
}
