/* Optional real Apache NimBLE central/HOGP application. All GAP/GATT/SMP calls
 * run in its MeOS task. Public commands enqueue work; pairing is never inferred
 * from an advertisement or an unauthenticated input notification. */
#include "../host.h"
#include <m5os/kmalloc.h>
#include <m5os/hosted_hci.h>
#include "nimble/nimble_port.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_sm.h"
#include "host/ble_uuid.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_hci.h"
#include "host/util/util.h"
#include "store/ram/ble_store_ram.h"
#define BT_DEVICE_LIMIT 8
#define BT_CHR_LIMIT 24
enum host_command { HOST_NONE,HOST_SCAN,HOST_CONNECT,HOST_DISCONNECT,HOST_REPLY,HOST_FORGET,HOST_LOST,HOST_RESTART };
static struct {
    bool started,synced,discovering,task_created;
    bt_host_status_t status;
    bt_advertisement_t devices[BT_DEVICE_LIMIT];
    bt_hogp_t hogp;
    struct ble_npl_event command_event;
    enum host_command pending;
    uint32_t duration,passkey;
    bool accept,justworks;
    ble_addr_t selected;
    struct ble_gatt_chr characteristics[BT_CHR_LIMIT];
    unsigned characteristic_count,descriptor_index;
    bt_hogp_handles_t handles;
    uint32_t write_token;
    struct {uint16_t connection;uint8_t action;uint32_t number,generation;} reply;
} bt_app;
static bool host_constructing,host_enabling;
static int host_gap(struct ble_gap_event *event,void *arg);
static void host_subscribe(void);
static void host_clear_pairing(void) {
    uint32_t flags=arch_irq_disable();
    bt_app.status.pairing_action=0;bt_app.status.comparison_number=0;
    if(!++bt_app.status.pairing_generation)++bt_app.status.pairing_generation;
    arch_irq_restore(flags);
}
static void host_error(int error) {
    bt_app.status.error=error;bt_app.status.state=BT_HOST_ERROR;
    host_clear_pairing();
    bt_hogp_security(&bt_app.hogp,bt_app.status.connection,false,false,0);
    bt_app.discovering=false;
}
static bool host_connection(uint16_t connection) {
    return bt_app.hogp.connected&&bt_app.hogp.connection==connection;
}
static int host_write(uint16_t connection,const struct ble_gatt_error *error,
                      struct ble_gatt_attr *attribute,void *arg) {
    (void)attribute;(void)arg;
    if(!host_connection(connection))return 0;
    bt_hogp_write_complete(&bt_app.hogp,connection,bt_app.write_token,error->status==0);
    if(error->status)host_error(error->status);else host_subscribe();
    return 0;
}
static void host_subscribe(void) {
    uint16_t attribute;uint8_t value[2];size_t length;uint32_t token;
    bt_hogp_action_t action=bt_hogp_next(&bt_app.hogp,&attribute,value,&length,&token);
    if(action==BT_HOGP_NO_ACTION) {
        if(bt_app.hogp.input.authenticated)bt_app.status.state=BT_HOST_INPUT;
        return;
    }
    bt_app.write_token=token;
    int rc;
    if(action==BT_HOGP_WRITE_PROTOCOL) {
        rc=ble_gattc_write_no_rsp_flat(bt_app.status.connection,attribute,value,(uint16_t)length);
        bt_hogp_write_complete(&bt_app.hogp,bt_app.status.connection,token,rc==0);
        if(!rc)host_subscribe();
    } else {
        rc=ble_gattc_write_flat(bt_app.status.connection,attribute,value,(uint16_t)length,host_write,NULL);
    }
    if(rc)host_error(rc);
}
static int host_descriptors(uint16_t connection,const struct ble_gatt_error *error,
                            uint16_t characteristic,const struct ble_gatt_dsc *dsc,void *arg);
static void host_next_descriptors(void) {
    while(bt_app.descriptor_index<bt_app.characteristic_count) {
        unsigned i=bt_app.descriptor_index++;
        const struct ble_gatt_chr *chr=&bt_app.characteristics[i];
        if(chr->val_handle!=bt_app.handles.keyboard_value&&chr->val_handle!=bt_app.handles.mouse_value)continue;
        uint16_t end=i+1<bt_app.characteristic_count?(uint16_t)(bt_app.characteristics[i+1].def_handle-1):bt_app.handles.service_end;
        if(chr->val_handle>=end) {host_error(BLE_HS_ENOTSUP);return;}
        int rc=ble_gattc_disc_all_dscs(bt_app.status.connection,chr->val_handle,end,host_descriptors,NULL);
        if(rc)host_error(rc);
        return;
    }
    if(!bt_hogp_discovered(&bt_app.hogp,bt_app.status.connection,&bt_app.handles)) {host_error(BLE_HS_ENOTSUP);return;}
    bt_app.discovering=false;host_subscribe();
}
static int host_descriptors(uint16_t connection,const struct ble_gatt_error *error,
                            uint16_t characteristic,const struct ble_gatt_dsc *dsc,void *arg) {
    (void)arg;
    if(!host_connection(connection)||!bt_app.discovering)return BLE_HS_EDONE;
    if(error->status==BLE_HS_EDONE) {host_next_descriptors();return 0;}
    if(error->status||!dsc) {host_error(error->status?error->status:BLE_HS_EBADDATA);return BLE_HS_EDONE;}
    if(ble_uuid_u16(&dsc->uuid.u)==0x2902) {
        uint16_t *ccc=characteristic==bt_app.handles.keyboard_value?&bt_app.handles.keyboard_ccc:
                      characteristic==bt_app.handles.mouse_value?&bt_app.handles.mouse_ccc:NULL;
        if(!ccc||*ccc) {host_error(BLE_HS_EBADDATA);return BLE_HS_EDONE;}
        *ccc=dsc->handle;
    }
    return 0;
}
static int host_characteristics(uint16_t connection,const struct ble_gatt_error *error,
                                const struct ble_gatt_chr *chr,void *arg) {
    (void)arg;
    if(!host_connection(connection)||!bt_app.discovering)return BLE_HS_EDONE;
    if(error->status==BLE_HS_EDONE) {host_next_descriptors();return 0;}
    if(error->status||!chr||bt_app.characteristic_count==BT_CHR_LIMIT) {
        host_error(error->status?error->status:BLE_HS_ENOMEM);return BLE_HS_EDONE;
    }
    if(chr->def_handle<=bt_app.handles.service_start||chr->val_handle<=chr->def_handle||
       chr->val_handle>bt_app.handles.service_end||
       (bt_app.characteristic_count&&chr->def_handle<=bt_app.characteristics[bt_app.characteristic_count-1].val_handle)) {
        host_error(BLE_HS_EBADDATA);return BLE_HS_EDONE;
    }
    bt_app.characteristics[bt_app.characteristic_count++]=*chr;
    uint16_t uuid=ble_uuid_u16(&chr->uuid.u),*value=NULL;
    if(uuid==0x2a4e&&(chr->properties&0x04))value=&bt_app.handles.protocol_mode;
    if(uuid==0x2a22&&(chr->properties&0x10))value=&bt_app.handles.keyboard_value;
    if(uuid==0x2a33&&(chr->properties&0x10))value=&bt_app.handles.mouse_value;
    if(value) {
        if(*value) {host_error(BLE_HS_EBADDATA);return BLE_HS_EDONE;}
        *value=chr->val_handle;
    }
    return 0;
}
static int host_service(uint16_t connection,const struct ble_gatt_error *error,
                        const struct ble_gatt_svc *service,void *arg) {
    (void)arg;
    if(!host_connection(connection)||!bt_app.discovering)return BLE_HS_EDONE;
    if(error->status==BLE_HS_EDONE) {
        if(!bt_app.handles.service_start) {host_error(BLE_HS_ENOTSUP);return 0;}
        int rc=ble_gattc_disc_all_chrs(connection,bt_app.handles.service_start,bt_app.handles.service_end,host_characteristics,NULL);
        if(rc)host_error(rc);
        return 0;
    }
    if(error->status||!service||bt_app.handles.service_start||service->start_handle>=service->end_handle) {
        host_error(error->status?error->status:BLE_HS_ENOTSUP);return BLE_HS_EDONE;
    }
    bt_app.handles.service_start=service->start_handle;bt_app.handles.service_end=service->end_handle;
    return 0;
}
static void host_begin_discovery(void) {
    const ble_uuid16_t hid=BLE_UUID16_INIT(0x1812);
    memset(&bt_app.handles,0,sizeof bt_app.handles);
    bt_app.characteristic_count=bt_app.descriptor_index=0;bt_app.discovering=true;
    bt_app.status.state=BT_HOST_DISCOVERING;
    int rc=ble_gattc_disc_svc_by_uuid(bt_app.status.connection,&hid.u,host_service,NULL);
    if(rc)host_error(rc);
}
static void host_advertisement(const struct ble_gap_disc_desc *disc) {
    unsigned i;
    for(i=0;i<bt_app.status.devices;i++)
        if(bt_app.devices[i].address_type==disc->addr.type&&!memcmp(bt_app.devices[i].address,disc->addr.val,6))break;
    if(i==BT_DEVICE_LIMIT)return;
    if(i==bt_app.status.devices)bt_app.status.devices++;
    bt_advertisement_t *out=&bt_app.devices[i];memset(out,0,sizeof *out);
    out->address_type=disc->addr.type;memcpy(out->address,disc->addr.val,6);out->event_type=disc->event_type;out->rssi=disc->rssi;
    struct ble_hs_adv_fields fields;
    if(ble_hs_adv_parse_fields(&fields,disc->data,disc->length_data))return;
    for(unsigned j=0;j<fields.num_uuids16;j++)if(ble_uuid_u16(&fields.uuids16[j].u)==0x1812)out->hid_service=true;
    size_t n=fields.name_len<sizeof(out->name)-1?fields.name_len:sizeof(out->name)-1;
    for(size_t j=0;j<n;j++)out->name[j]=(fields.name[j]<32||fields.name[j]==127)?'?':(char)fields.name[j];
}
static int host_gap(struct ble_gap_event *event,void *arg) {
    (void)arg;
    switch(event->type) {
    case BLE_GAP_EVENT_DISC:host_advertisement(&event->disc);break;
    case BLE_GAP_EVENT_DISC_COMPLETE:if(bt_app.status.state==BT_HOST_SCANNING)bt_app.status.state=BT_HOST_READY;break;
    case BLE_GAP_EVENT_CONNECT:
        if(event->connect.status) {host_error(event->connect.status);break;}
        bt_app.status.connection=event->connect.conn_handle;
        host_clear_pairing();
        {struct ble_gap_conn_desc peer;
         if(ble_gap_conn_find(event->connect.conn_handle,&peer)||peer.peer_ota_addr.type!=bt_app.selected.type||
            memcmp(peer.peer_ota_addr.val,bt_app.selected.val,6)) {
             ble_gap_terminate(event->connect.conn_handle,BLE_ERR_REM_USER_CONN_TERM);host_error(BLE_HS_EAUTHEN);break;
         }}
        if(!bt_hogp_connect(&bt_app.hogp,event->connect.conn_handle)) {host_error(BLE_HS_EBUSY);break;}
        bt_app.status.state=BT_HOST_PAIRING;
        {int rc=ble_gap_security_initiate(event->connect.conn_handle);if(rc)host_error(rc);}
        break;
    case BLE_GAP_EVENT_ENC_CHANGE: {
        if(!host_connection(event->enc_change.conn_handle))break;
        struct ble_gap_conn_desc d;
        if(event->enc_change.status||ble_gap_conn_find(event->enc_change.conn_handle,&d)) {
            host_error(event->enc_change.status?event->enc_change.status:BLE_HS_ENOTCONN);break;
        }
        bt_app.status.encrypted=d.sec_state.encrypted;bt_app.status.authenticated=d.sec_state.authenticated;
        bt_app.status.bonded=d.sec_state.bonded;host_clear_pairing();
        bool accepted=d.sec_state.authenticated||bt_app.status.justworks_allowed;
        bt_hogp_security(&bt_app.hogp,event->enc_change.conn_handle,d.sec_state.encrypted,accepted,d.sec_state.key_size);
        if(!bt_app.hogp.secure) {
            ble_gap_terminate(event->enc_change.conn_handle,BLE_ERR_REM_USER_CONN_TERM);
            host_error(BLE_HS_EAUTHEN);break;
        }
        host_begin_discovery();break;
    }
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if(!host_connection(event->passkey.conn_handle))break;
        host_clear_pairing();
        uint32_t comparison=event->passkey.params.numcmp;
        if(event->passkey.params.action==BLE_SM_IOACT_DISP) {
            uint32_t random=UINT32_MAX;unsigned attempts=0;
            /* Rejection sampling avoids modulo bias in the displayed passkey. */
            do {if(++attempts>8||ble_hs_hci_rand(&random,sizeof random)) {host_error(BLE_HS_ECONTROLLER);return 0;}}
            while(random>=4294000000u);
            comparison=random%1000000u;
        }
        {uint32_t flags=arch_irq_disable();
         bt_app.status.comparison_number=comparison;bt_app.status.pairing_action=event->passkey.params.action;
         arch_irq_restore(flags);}
        /* No automatic NUMCMP, INPUT, OOB or display-passkey acceptance. */
        break;
    case BLE_GAP_EVENT_NOTIFY_RX: {
        uint8_t report[8];size_t n=OS_MBUF_PKTLEN(event->notify_rx.om);
        if(n<=sizeof report&&!os_mbuf_copydata(event->notify_rx.om,0,(int)n,report)&&
           bt_hogp_notify(&bt_app.hogp,event->notify_rx.conn_handle,event->notify_rx.attr_handle,report,n))bt_app.status.reports++;
        break;
    }
    case BLE_GAP_EVENT_DISCONNECT:
        bt_hogp_disconnect(&bt_app.hogp,event->disconnect.conn.conn_handle);
        bt_app.status.encrypted=bt_app.status.authenticated=bt_app.status.bonded=false;
        host_clear_pairing();bt_app.discovering=false;bt_app.status.state=bt_app.synced?BT_HOST_READY:BT_HOST_ERROR;
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:return BLE_GAP_REPEAT_PAIRING_IGNORE;
    default:break;
    }
    return 0;
}
static void host_sync(void) {
    int rc=ble_hs_util_ensure_addr(0);
    if(rc)host_error(rc);else {bt_app.synced=true;bt_app.status.state=BT_HOST_READY;}
}
static void host_reset(int reason) {bt_app.synced=false;host_error(reason);}
static void host_command_run(struct ble_npl_event *event) {
    (void)event;
    uint32_t flags=arch_irq_disable();enum host_command cmd=bt_app.pending;
    uint32_t duration=bt_app.duration,passkey=bt_app.passkey;
    bool accept=bt_app.accept,justworks=bt_app.justworks;ble_addr_t selected=bt_app.selected;
    uint16_t reply_connection=bt_app.reply.connection;uint8_t reply_action=bt_app.reply.action;
    uint32_t reply_number=bt_app.reply.number,reply_generation=bt_app.reply.generation;
    bt_app.pending=HOST_NONE;arch_irq_restore(flags);
    int rc=0;
    if(cmd==HOST_SCAN) {
        if(bt_app.status.state!=BT_HOST_READY)return;
        struct ble_gap_disc_params p={0};p.passive=1;p.filter_duplicates=1;p.itvl=160;p.window=80;
        bt_app.status.devices=0;memset(bt_app.devices,0,sizeof bt_app.devices);
        rc=ble_gap_disc(BLE_OWN_ADDR_PUBLIC,(int32_t)duration,&p,host_gap,NULL);
        if(!rc)bt_app.status.state=BT_HOST_SCANNING;
    } else if(cmd==HOST_CONNECT) {
        if(bt_app.status.state!=BT_HOST_READY)return;
        ble_hs_cfg.sm_mitm=!justworks;
        bt_app.status.justworks_allowed=justworks;
        rc=ble_gap_connect(BLE_OWN_ADDR_PUBLIC,&selected,10000,NULL,host_gap,NULL);
        if(!rc)bt_app.status.state=BT_HOST_CONNECTING;
    } else if(cmd==HOST_DISCONNECT) {
        if(bt_app.hogp.connected)rc=ble_gap_terminate(bt_app.status.connection,BLE_ERR_REM_USER_CONN_TERM);
        else if(bt_app.status.state==BT_HOST_CONNECTING)rc=ble_gap_conn_cancel();
        else rc=ble_gap_disc_cancel();
    } else if(cmd==HOST_REPLY) {
        if(!host_connection(reply_connection)||reply_connection!=bt_app.status.connection||
           reply_action!=bt_app.status.pairing_action||reply_number!=bt_app.status.comparison_number||
           reply_generation!=bt_app.status.pairing_generation)return;
        if(!accept)rc=ble_gap_terminate(bt_app.status.connection,BLE_ERR_REM_USER_CONN_TERM);
        else {
            struct ble_sm_io io={0};io.action=reply_action;
            if(io.action==BLE_SM_IOACT_NUMCMP)io.numcmp_accept=1;
            else if(io.action==BLE_SM_IOACT_INPUT)io.passkey=passkey;
            else if(io.action==BLE_SM_IOACT_DISP)io.passkey=reply_number;
            else {host_error(BLE_HS_ENOTSUP);return;}
            rc=ble_sm_inject_io(bt_app.status.connection,&io);
            if(!rc)host_clear_pairing();
        }
        if(!accept&&!rc)host_clear_pairing();
    } else if(cmd==HOST_FORGET) {
        /* The upstream RAM store delete-all helper is driven through the
         * host utility so keys and CCC/security records are removed together. */
        rc=ble_store_clear();
    } else if(cmd==HOST_LOST) {
        bt_hogp_disconnect(&bt_app.hogp,bt_app.status.connection);
        bt_app.synced=false;host_error(BLE_HS_ECONTROLLER);
    } else if(cmd==HOST_RESTART) {
        bt_hogp_disconnect(&bt_app.hogp,bt_app.status.connection);bt_app.synced=false;
        bt_app.status.state=BT_HOST_STARTING;
        /* If upstream is already recovering, its retry timer owns resync.
         * Do not schedule duplicate reset reasons while bring-up is active. */
        if(ble_hs_synced())ble_hs_sched_reset(BLE_HS_ECONTROLLER);
    }
    if(rc)host_error(rc);
}
static bool host_post(enum host_command cmd) {
    if(!bt_app.started)return false;
    uint32_t flags=arch_irq_disable();
    if(bt_app.pending!=HOST_NONE) {arch_irq_restore(flags);return false;}
    bt_app.pending=cmd;ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&bt_app.command_event);
    arch_irq_restore(flags);return true;
}
static void host_task(void *arg) {(void)arg;nimble_port_run();}
bool bt_host_start(bt_hci_send_fn send,void *arg,bt_hid_key_fn key,bt_hid_mouse_fn mouse,void *input_arg) {
    uint32_t flags=arch_irq_disable();
    if(bt_app.started||host_constructing||!send) {arch_irq_restore(flags);return false;}
    host_constructing=true;arch_irq_restore(flags);
    memset(&bt_app,0,sizeof bt_app);bt_hogp_init(&bt_app.hogp,key,mouse,input_arg);
    meos_nimble_transport_configure(send,arg);nimble_port_init();
    ble_hs_cfg.sync_cb=host_sync;ble_hs_cfg.reset_cb=host_reset;
    ble_hs_cfg.sm_io_cap=BLE_HS_IO_KEYBOARD_DISPLAY;ble_hs_cfg.sm_bonding=1;ble_hs_cfg.sm_sc=1;ble_hs_cfg.sm_mitm=1;
    ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_read_cb=ble_store_ram_read;ble_hs_cfg.store_write_cb=ble_store_ram_write;ble_hs_cfg.store_delete_cb=ble_store_ram_delete;
    ble_npl_event_init(&bt_app.command_event,host_command_run,NULL);
    bt_app.status.state=BT_HOST_STARTING;bt_app.started=true;
    if(task_create_stack("bluetooth",host_task,NULL,TASK_PRIO_DEFAULT,12288)<0) {
        host_error(BLE_HS_ENOMEM);host_constructing=false;return false;
    }
    bt_app.task_created=true;host_constructing=false;
    return true;
}
bool bt_host_scan(uint32_t duration) {
    uint32_t flags=arch_irq_disable();
    if(bt_app.pending!=HOST_NONE) {arch_irq_restore(flags);return false;}
    if(!bt_app.synced||bt_app.status.state!=BT_HOST_READY||!duration||duration>60000) {arch_irq_restore(flags);return false;}
    bt_app.duration=duration;bool ok=host_post(HOST_SCAN);arch_irq_restore(flags);return ok;
}
bool bt_host_connect(const uint8_t address[6],uint8_t type,bool justworks) {
    uint32_t flags=arch_irq_disable();
    if(bt_app.pending!=HOST_NONE||!address||type>1||!bt_app.synced||bt_app.status.state!=BT_HOST_READY) {arch_irq_restore(flags);return false;}
    memcpy(bt_app.selected.val,address,6);bt_app.selected.type=type;bt_app.justworks=justworks;
    bool ok=host_post(HOST_CONNECT);arch_irq_restore(flags);return ok;
}
bool bt_host_disconnect(void) {return host_post(HOST_DISCONNECT);}
bool bt_host_pair_reply(bool accept,uint32_t passkey) {
    /* Untokened UI approval cannot be tied to an observed challenge. */
    if(accept)return false;
    uint32_t flags=arch_irq_disable();uint32_t generation=bt_app.status.pairing_generation;
    bool ok=bt_host_pair_reply_for(generation,accept,passkey);arch_irq_restore(flags);return ok;
}
bool bt_host_pair_reply_for(uint32_t generation,bool accept,uint32_t passkey) {
    uint32_t flags=arch_irq_disable();
    if(bt_app.pending!=HOST_NONE||!bt_app.status.pairing_action||passkey>999999||
       !generation||generation!=bt_app.status.pairing_generation) {arch_irq_restore(flags);return false;}
    bt_app.accept=accept;bt_app.passkey=passkey;
    bt_app.reply.connection=bt_app.status.connection;bt_app.reply.action=bt_app.status.pairing_action;
    bt_app.reply.number=bt_app.status.comparison_number;bt_app.reply.generation=generation;
    bool ok=host_post(HOST_REPLY);arch_irq_restore(flags);return ok;
}
bool bt_host_forget_bonds(void) {
    if(bt_app.status.state!=BT_HOST_READY)return false;
    return host_post(HOST_FORGET);
}
void bt_host_status(bt_host_status_t *out) {
    if(!out)return;
    uint32_t flags=arch_irq_disable();*out=bt_app.status;arch_irq_restore(flags);
}
size_t bt_host_devices(bt_advertisement_t *out,size_t capacity) {
    if(!out)return 0;
    uint32_t flags=arch_irq_disable();size_t n=bt_app.status.devices<capacity?bt_app.status.devices:capacity;
    memcpy(out,bt_app.devices,n*sizeof *out);arch_irq_restore(flags);return n;
}
bool bt_host_receive(uint8_t type,const uint8_t *p,size_t n) {
    return bt_app.started&&meos_nimble_receive(type,p,n);
}
void bt_host_transport_lost(void) {
    if(!bt_app.started)return;
    host_clear_pairing();
    uint32_t flags=arch_irq_disable();bt_app.pending=HOST_LOST;
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&bt_app.command_event);arch_irq_restore(flags);
}
static bool host_sdio_receive(void *arg,uint8_t type,const uint8_t *packet,size_t length) {
    (void)arg;return bt_host_receive(type,packet,length);
}
static void host_sdio_lost(void *arg) {(void)arg;bt_host_transport_lost();}
static bool host_sdio_send(void *arg,uint8_t type,const uint8_t *packet,size_t length) {
    (void)arg;return hosted_hci_send(type,packet,length);
}
bool bt_host_enable(bt_hid_key_fn key,bt_hid_mouse_fn mouse,void *arg) {
    uint32_t flags=arch_irq_disable();
    if(host_enabling||host_constructing) {arch_irq_restore(flags);return false;}
    host_enabling=true;arch_irq_restore(flags);
    bool ok=false;
    if(!hosted_hci_available()||!hosted_hci_claim(host_sdio_receive,host_sdio_lost,&bt_app))goto done;
    if(bt_app.started) {
        if(!bt_app.task_created) {hosted_hci_release(&bt_app);goto done;}
        if(bt_app.synced&&bt_app.status.state!=BT_HOST_ERROR)ok=true;
        else ok=host_post(HOST_RESTART);
        goto done;
    }
    ok=bt_host_start(host_sdio_send,NULL,key,mouse,arg);
    if(!ok)hosted_hci_release(&bt_app);
done:
    flags=arch_irq_disable();host_enabling=false;arch_irq_restore(flags);return ok;
}
