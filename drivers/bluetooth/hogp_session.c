/* Original secure boot-HOGP callback adapter. Pairing/crypto/GATT operations
 * remain the responsibility of an audited host stack; no security is simulated. */
#include "bluetooth.h"
#include <string.h>
static bool owns(const bt_hogp_t *h,uint16_t connection) {return h&&h->connected&&h->connection==connection;}
static void revoke(bt_hogp_t *h) {
    bt_hid_boot_set_authenticated(&h->input,false);
    h->secure=false;h->protocol_set=false;
    h->keyboard_subscribed=h->mouse_subscribed=false;
    h->write_pending=false;h->action=BT_HOGP_NO_ACTION;h->write_token++;
}
void bt_hogp_init(bt_hogp_t *h,bt_hid_key_fn key,bt_hid_mouse_fn mouse,void *arg) {
    if(!h)return;
    memset(h,0,sizeof *h);bt_hid_boot_init(&h->input,key,mouse,arg);
}
bool bt_hogp_connect(bt_hogp_t *h,uint16_t connection) {
    if(!h||h->connected||connection>0x0eff)return false;
    revoke(h);memset(&h->handles,0,sizeof h->handles);
    h->connection=connection;h->connected=true;return true;
}
static bool inside(const bt_hogp_handles_t *g,uint16_t value) {
    return value>g->service_start&&value<=g->service_end;
}
bool bt_hogp_discovered(bt_hogp_t *h,uint16_t connection,const bt_hogp_handles_t *g) {
    if(!owns(h,connection)||!g||h->write_pending||h->protocol_set)return false;
    if(!g->service_start||g->service_end<=g->service_start||!inside(g,g->protocol_mode))return false;
    if(!g->keyboard_value&&!g->mouse_value)return false;
    if((g->keyboard_value&&!inside(g,g->keyboard_value))||(g->keyboard_ccc&&!inside(g,g->keyboard_ccc))||
       (g->mouse_value&&!inside(g,g->mouse_value))||(g->mouse_ccc&&!inside(g,g->mouse_ccc)))return false;
    if((!g->keyboard_value)!=(!g->keyboard_ccc)||(!g->mouse_value)!=(!g->mouse_ccc))return false;
    if((g->keyboard_value&&g->keyboard_ccc<=g->keyboard_value)||
       (g->mouse_value&&g->mouse_ccc<=g->mouse_value))return false;
    uint16_t attributes[5]={g->protocol_mode,g->keyboard_value,g->keyboard_ccc,g->mouse_value,g->mouse_ccc};
    for(unsigned i=0;i<5;i++)for(unsigned j=0;j<i;j++)if(attributes[i]&&attributes[i]==attributes[j])return false;
    h->handles=*g;return true;
}
void bt_hogp_security(bt_hogp_t *h,uint16_t connection,bool encrypted,bool accepted,uint8_t bytes) {
    if(!owns(h,connection))return;
    if(!encrypted||!accepted||bytes!=16) {revoke(h);return;}
    h->secure=true;
}
bt_hogp_action_t bt_hogp_next(bt_hogp_t *h,uint16_t *attribute,uint8_t value[2],size_t *length,uint32_t *token) {
    if(!h||!attribute||!value||!length||!token||!h->connected||!h->secure||h->write_pending||!h->handles.protocol_mode)
        return BT_HOGP_NO_ACTION;
    bt_hogp_action_t a=BT_HOGP_NO_ACTION;
    if(!h->protocol_set) {a=BT_HOGP_WRITE_PROTOCOL;*attribute=h->handles.protocol_mode;value[0]=0;*length=1;}
    else if(h->handles.keyboard_value&&!h->keyboard_subscribed) {
        a=BT_HOGP_SUBSCRIBE_KEYBOARD;*attribute=h->handles.keyboard_ccc;value[0]=1;value[1]=0;*length=2;
    } else if(h->handles.mouse_value&&!h->mouse_subscribed) {
        a=BT_HOGP_SUBSCRIBE_MOUSE;*attribute=h->handles.mouse_ccc;value[0]=1;value[1]=0;*length=2;
    }
    if(a!=BT_HOGP_NO_ACTION) {h->write_pending=true;h->action=a;*token=++h->write_token;}
    return a;
}
void bt_hogp_write_complete(bt_hogp_t *h,uint16_t connection,uint32_t token,bool success) {
    if(!owns(h,connection)||!h->write_pending||!h->secure||token!=h->write_token)return;
    if(!success) {revoke(h);return;}
    if(h->action==BT_HOGP_WRITE_PROTOCOL)h->protocol_set=true;
    else if(h->action==BT_HOGP_SUBSCRIBE_KEYBOARD)h->keyboard_subscribed=true;
    else if(h->action==BT_HOGP_SUBSCRIBE_MOUSE)h->mouse_subscribed=true;
    h->write_pending=false;h->action=BT_HOGP_NO_ACTION;h->write_token++;
    if(h->protocol_set&&(h->keyboard_subscribed||h->mouse_subscribed))
        bt_hid_boot_set_authenticated(&h->input,true);
}
bool bt_hogp_notify(bt_hogp_t *h,uint16_t connection,uint16_t attribute,const uint8_t *report,size_t length) {
    if(!owns(h,connection)||!h->secure||!h->protocol_set)return false;
    if(h->keyboard_subscribed&&attribute==h->handles.keyboard_value)
        return bt_hid_boot_keyboard(&h->input,report,length);
    if(h->mouse_subscribed&&attribute==h->handles.mouse_value)
        return bt_hid_boot_mouse(&h->input,report,length);
    return false;
}
void bt_hogp_disconnect(bt_hogp_t *h,uint16_t connection) {
    if(!owns(h,connection))return;
    revoke(h);h->connected=false;memset(&h->handles,0,sizeof h->handles);
}
