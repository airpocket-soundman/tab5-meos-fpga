#include "bt_app.h"
#if defined(M5OS_BLUETOOTH) && M5OS_BLUETOOTH
#include "../../drivers/bluetooth/host.h"
#include "../../drivers/usbkbd/hid_keys.h"
#include "../../drivers/lcd/fbcon.h"
#include <m5os/ime.h>
#include <m5os/gui.h>
#include <m5os/mutex.h>
#include <m5os/kprintf.h>
#include <string.h>
enum pointer_mode { POINTER_CONSOLE,POINTER_GUI,POINTER_OFF };
static struct {
    uint8_t modifiers;
    bool caps,initialized,mouse_down,left_pressed;
    window_t touch_window;
    int x,y;
    enum pointer_mode pointer;
} input;
static mutex_t input_lock=MUTEX_INIT;
static void keyboard_input(void *arg,uint8_t usage,bool pressed) {
    (void)arg;mutex_lock(&input_lock);
    if(usage>=0xe0&&usage<=0xe7) {
        uint8_t mask=(uint8_t)(1u<<(usage-0xe0));
        if(pressed)input.modifiers|=mask;else input.modifiers&=(uint8_t)~mask;
    } else if(pressed&&usage==HID_USAGE_CAPSLOCK)input.caps=!input.caps;
    else if(pressed) {
        char bytes[8];hid_decode(usage,input.modifiers,input.caps,bytes);
        if(bytes[0])ime_feed(bytes);
    }
    mutex_unlock(&input_lock);
}
static int bounded(int value,int maximum) {return value<0?0:value>=maximum?maximum-1:value;}
static void mouse_input(void *arg,uint8_t buttons,int16_t dx,int16_t dy,int16_t wheel) {
    (void)arg;mutex_lock(&input_lock);
    input.x=bounded(input.x+dx,GUI_SCREEN_W);input.y=bounded(input.y+dy,GUI_SCREEN_H);
    bool down=(buttons&1u)!=0,previous=input.left_pressed;input.left_pressed=down;
    window_t front=input.pointer==POINTER_GUI?gui_front_window():0;
    if(input.mouse_down&&front!=input.touch_window) {
        gui_inject_touch(GUI_TOUCH_UP,input.x,input.y);input.mouse_down=false;
    }
    if(input.pointer==POINTER_GUI&&front) {
        if(down&&!previous) {
            gui_inject_touch(GUI_TOUCH_DOWN,input.x,input.y);input.mouse_down=true;input.touch_window=front;
        }
        else if(down&&input.mouse_down&&(dx||dy))gui_inject_touch(GUI_TOUCH_MOVE,input.x,input.y);
        else if(!down&&input.mouse_down)gui_inject_touch(GUI_TOUCH_UP,input.x,input.y);
        if(!down)input.mouse_down=false;
    } else {
        if(input.mouse_down)gui_inject_touch(GUI_TOUCH_UP,input.x,input.y);
        input.mouse_down=false;
        if(input.pointer==POINTER_CONSOLE&&wheel&&!fbcon_is_paused())fbcon_scroll((int)wheel*3);
    }
    mutex_unlock(&input_lock);
}
static void initialize_input(void) {
    mutex_lock(&input_lock);
    if(!input.initialized) {
        input.x=GUI_SCREEN_W/2;input.y=GUI_SCREEN_H/2;
        input.pointer=POINTER_CONSOLE;input.initialized=true;
    }
    mutex_unlock(&input_lock);
}
static int usage(void (*output)(const char *)) {
    output("bt on|status|scan [MS]|devices\n"
           "bt connect MAC public|random [justworks]\n"
           "bt pair accept [SIX_DIGIT_PASSKEY] challenge=N|reject\n"
           "bt disconnect|forget|pointer gui|console|off\n");return 1;
}
static bool number(const char *text,uint32_t maximum,uint32_t *out) {
    if(!text||!*text)return false;
    uint32_t n=0;
    for(const char *p=text;*p;p++) {
        if(*p<'0'||*p>'9'||n>(maximum-(uint32_t)(*p-'0'))/10u)return false;
        n=n*10u+(uint32_t)(*p-'0');
        if(n>maximum)return false;
    }
    *out=n;return true;
}
static int hex_digit(char c) {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
static bool address(const char *text,uint8_t out[6]) {
    if(!text||strlen(text)!=17)return false;
    unsigned any=0,all=0xff;
    for(unsigned i=0;i<6;i++) {
        int hi=hex_digit(text[i*3]),lo=hex_digit(text[i*3+1]);
        if(hi<0||lo<0||(i<5&&text[i*3+2]!=':'))return false;
        uint8_t b=(uint8_t)((hi<<4)|lo);out[5-i]=b;any|=b;all&=b;
    }
    return any&&all!=0xff;
}
static void show_status(void (*output)(const char *)) {
    static const char *const names[]={"off","starting","ready","scanning","connecting","pairing","discovering","input","error"};
    bt_host_status_t status;bt_host_status(&status);char line[224];
    const char *state=(unsigned)status.state<sizeof names/sizeof names[0]?names[status.state]:"unknown";
    ksnprintf(line,sizeof line,"state=%s error=%d connection=%u encrypted=%u authenticated=%u bonded=%u policy=%s devices=%u reports=%u bonds=RAM\n",
              state,status.error,(unsigned)status.connection,(unsigned)status.encrypted,(unsigned)status.authenticated,
              (unsigned)status.bonded,status.justworks_allowed?"explicit-justworks":"MITM",status.devices,status.reports);
    output(line);
    if(status.pairing_action) {
        ksnprintf(line,sizeof line,"Pairing challenge=%u\n",(unsigned)status.pairing_generation);output(line);
    }
    if(status.pairing_action==2)output("Pairing requires: bt pair accept SIX_DIGIT_PASSKEY challenge=N\n");
    else if(status.pairing_action==3||status.pairing_action==4) {
        ksnprintf(line,sizeof line,status.pairing_action==3?
                  "Pairing passkey: %06u. Enter on the peer, then bt pair accept challenge=N; or bt pair reject.\n":
                  "Compare %06u on both devices. If equal: bt pair accept challenge=N; otherwise: bt pair reject.\n",
                  (unsigned)status.comparison_number);output(line);
    } else if(status.pairing_action)output("This pairing method is unsupported; bt pair reject.\n");
    mutex_lock(&input_lock);
    ksnprintf(line,sizeof line,"pointer=%s x=%d y=%d caps=%u\n",
              input.pointer==POINTER_GUI?"gui":input.pointer==POINTER_OFF?"off":"console",input.x,input.y,(unsigned)input.caps);
    mutex_unlock(&input_lock);output(line);
}
static void show_devices(void (*output)(const char *)) {
    bt_advertisement_t devices[8];size_t n=bt_host_devices(devices,8);char line[160];
    if(!n)output("No discovered devices. Use bt scan, then bt devices.\n");
    for(size_t i=0;i<n;i++) {
        const bt_advertisement_t *d=&devices[i];
        ksnprintf(line,sizeof line,"%02x:%02x:%02x:%02x:%02x:%02x %s rssi=%d HID=%u %s\n",
                  d->address[5],d->address[4],d->address[3],d->address[2],d->address[1],d->address[0],
                  d->address_type==1?"random":"public",(int)d->rssi,(unsigned)d->hid_service,d->name);output(line);
    }
}
int bt_app_command(int argc,char **argv,void (*output)(const char *)) {
    if(!output)return 1;
    if(argc<2)return usage(output);
    bool ok=false;
    if(argc==2&&!strcmp(argv[1],"on")) {
        initialize_input();ok=bt_host_enable(keyboard_input,mouse_input,NULL);
    } else if(argc==2&&!strcmp(argv[1],"status")) {show_status(output);return 0;}
    else if(argc==2&&!strcmp(argv[1],"devices")) {show_devices(output);return 0;}
    else if((argc==2||argc==3)&&!strcmp(argv[1],"scan")) {
        uint32_t duration=10000;if(argc==3&&(!number(argv[2],60000,&duration)||!duration))return usage(output);
        ok=bt_host_scan(duration);
    } else if((argc==4||argc==5)&&!strcmp(argv[1],"connect")) {
        uint8_t peer[6],type;
        if(!address(argv[2],peer))return usage(output);
        if(!strcmp(argv[3],"public"))type=0;
        else if(!strcmp(argv[3],"random"))type=1;else return usage(output);
        if(argc==5&&strcmp(argv[4],"justworks"))return usage(output);
        ok=bt_host_connect(peer,type,argc==5);
    } else if(argc>=3&&!strcmp(argv[1],"pair")) {
        if(argc==3&&!strcmp(argv[2],"reject"))ok=bt_host_pair_reply(false,0);
        else if(argc>=3&&argc<=5&&!strcmp(argv[2],"accept")) {
            uint32_t passkey=0;bt_host_status_t status;bt_host_status(&status);
            const char *token=argc>=4?argv[argc-1]:"";uint32_t generation;
            if(strncmp(token,"challenge=",10)||!number(token+10,UINT32_MAX,&generation)||!generation) {
                output("Use the explicit challenge=N shown by bt status.\n");return 1;
            }
            if(argc==5&&(strlen(argv[3])!=6||!number(argv[3],999999,&passkey)))return usage(output);
            if(status.pairing_action==2&&argc!=5) {
                output("Passkey input requires six explicit digits.\n");return 1;
            }
            if(argc==5&&status.pairing_action!=2) {
                output("A passkey value is accepted only for passkey-input pairing.\n");return 1;
            }
            ok=bt_host_pair_reply_for(generation,true,passkey);
        } else return usage(output);
    } else if(argc==2&&!strcmp(argv[1],"disconnect"))ok=bt_host_disconnect();
    else if(argc==2&&!strcmp(argv[1],"forget"))ok=bt_host_forget_bonds();
    else if(argc==3&&!strcmp(argv[1],"pointer")) {
        enum pointer_mode mode;
        if(!strcmp(argv[2],"gui"))mode=POINTER_GUI;
        else if(!strcmp(argv[2],"console"))mode=POINTER_CONSOLE;
        else if(!strcmp(argv[2],"off"))mode=POINTER_OFF;else return usage(output);
        initialize_input();mutex_lock(&input_lock);
        if(input.mouse_down)gui_inject_touch(GUI_TOUCH_UP,input.x,input.y);
        input.mouse_down=false;input.pointer=mode;mutex_unlock(&input_lock);
        output("Pointer routing updated. Boot Mouse reports do not include a wheel.\n");return 0;
    } else return usage(output);
    output(ok?"Request accepted. Use bt status for completion/pairing prompts.\n":"Request rejected: unavailable, busy, or invalid current state; use bt status.\n");
    return ok?0:1;
}
#else
int bt_app_command(int argc,char **argv,void (*output)(const char *)) {
    (void)argc;(void)argv;
    if(output)output("Bluetooth is not built into this firmware (M5OS_BLUETOOTH=ON required).\n");
    return 1;
}
#endif
