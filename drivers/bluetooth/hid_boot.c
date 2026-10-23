/* Original boot HID decoder. USB HID usages are shared by BLE HID over GATT. */
#include "bluetooth.h"
#include <string.h>
void bt_hid_boot_init(bt_hid_boot_t *h,bt_hid_key_fn key,bt_hid_mouse_fn mouse,void *arg) {
    if(!h)return;
    memset(h,0,sizeof *h);h->key=key;h->mouse=mouse;h->arg=arg;
}
static bool contains(const uint8_t keys[6],uint8_t usage) {
    for(unsigned i=0;i<6;i++)if(keys[i]==usage)return true;
    return false;
}
void bt_hid_boot_set_authenticated(bt_hid_boot_t *h,bool authenticated) {
    if(!h)return;
    if(h->authenticated&&!authenticated) {
        if(h->key) {
            for(unsigned i=0;i<8;i++)if(h->modifiers&(1u<<i))h->key(h->arg,(uint8_t)(0xe0+i),false);
            for(unsigned i=0;i<6;i++)if(h->keys[i])h->key(h->arg,h->keys[i],false);
        }
        if(h->mouse)h->mouse(h->arg,0,0,0,0);
        h->modifiers=0;memset(h->keys,0,sizeof h->keys);
    }
    h->authenticated=authenticated;
}
bool bt_hid_boot_keyboard(bt_hid_boot_t *h,const uint8_t *r,size_t n) {
    if(!h||!h->authenticated||!r||n!=8||r[1])return false;
    /* ErrorRollOver, POSTFail, ErrorUndefined: preserve prior key state. */
    for(unsigned i=2;i<8;i++) {
        if(r[i]>=1&&r[i]<=3)return false;
        for(unsigned j=2;j<i;j++)if(r[i]&&r[i]==r[j])return false;
    }
    if(h->key) {
        for(unsigned i=0;i<8;i++)if((h->modifiers^r[0])&(1u<<i))
            h->key(h->arg,(uint8_t)(0xe0+i),(r[0]&(1u<<i))!=0);
        for(unsigned i=0;i<6;i++)if(h->keys[i]&&!contains(r+2,h->keys[i]))h->key(h->arg,h->keys[i],false);
        for(unsigned i=2;i<8;i++)if(r[i]&&!contains(h->keys,r[i]))h->key(h->arg,r[i],true);
    }
    h->modifiers=r[0];memcpy(h->keys,r+2,6);return true;
}
bool bt_hid_boot_mouse(bt_hid_boot_t *h,const uint8_t *r,size_t n) {
    /* BLE Boot Mouse Input Report is exactly buttons, signed X, signed Y.
     * Wheel/extra-button reports require the Report Map parser, not boot mode. */
    if(!h||!h->authenticated||!r||n!=3||(r[0]&0xf8))return false;
    if(h->mouse)h->mouse(h->arg,r[0],(int8_t)r[1],(int8_t)r[2],0);
    return true;
}
