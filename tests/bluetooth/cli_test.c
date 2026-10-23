#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../../user/bluetooth/bt_app.c"
static bt_hid_key_fn key_callback;
static bt_hid_mouse_fn mouse_callback;
static bt_host_status_t status={.state=BT_HOST_READY,.pairing_generation=9};
static unsigned enables,connects,pairs,scans,forgets,disconnects;
static uint32_t scan_duration,pair_passkey;
static uint8_t peer[6],peer_type;
static bool justworks,accepted,paused;
static window_t front_window;
static char output_text[4096],typed[64];
static unsigned touches;static int kinds[16],touch_x,touch_y,scrolled;
int ksnprintf(char *buf,size_t capacity,const char *format,...) {
 va_list args;va_start(args,format);int n=vsnprintf(buf,capacity,format,args);va_end(args);return n;
}
void mutex_lock(mutex_t *lock) {(void)lock;}
void mutex_unlock(mutex_t *lock) {(void)lock;}
bool bt_host_enable(bt_hid_key_fn key,bt_hid_mouse_fn mouse,void *arg) {
 (void)arg;key_callback=key;mouse_callback=mouse;enables++;return true;
}
void bt_host_status(bt_host_status_t *out) {*out=status;}
size_t bt_host_devices(bt_advertisement_t *out,size_t capacity) {
 assert(capacity>=1);memset(out,0,sizeof *out);out->address[0]=6;out->address[1]=5;out->address[2]=4;
 out->address[3]=3;out->address[4]=2;out->address[5]=1;out->address_type=1;out->rssi=-50;
 memcpy(out->name,"Keyboard",9);return 1;
}
bool bt_host_connect(const uint8_t address[6],uint8_t type,bool jw) {
 memcpy(peer,address,6);peer_type=type;justworks=jw;connects++;return true;
}
bool bt_host_scan(uint32_t duration) {scan_duration=duration;scans++;return true;}
bool bt_host_pair_reply(bool accept,uint32_t passkey) {accepted=accept;pair_passkey=passkey;pairs++;return true;}
bool bt_host_pair_reply_for(uint32_t generation,bool accept,uint32_t passkey) {
 return generation==status.pairing_generation&&bt_host_pair_reply(accept,passkey);
}
bool bt_host_disconnect(void) {disconnects++;return true;}
bool bt_host_forget_bonds(void) {forgets++;return true;}
void ime_feed(const char *text) {strncat(typed,text,sizeof typed-strlen(typed)-1);}
window_t gui_front_window(void) {return front_window;}
void gui_inject_touch(int kind,int x,int y) {assert(touches<16);kinds[touches++]=kind;touch_x=x;touch_y=y;}
bool fbcon_is_paused(void) {return paused;}
void fbcon_scroll(int rows) {scrolled+=rows;}
static void write_output(const char *text) {strncat(output_text,text,sizeof output_text-strlen(output_text)-1);}
static int command(int argc,char **argv) {output_text[0]=0;return bt_app_command(argc,argv,write_output);}
#define CMD(...) do {char *args[]={__VA_ARGS__};assert(command(sizeof args/sizeof args[0],args)==0);} while(0)
#define BAD(...) do {char *args[]={__VA_ARGS__};assert(command(sizeof args/sizeof args[0],args)!=0);} while(0)
int main(void) {
 BAD("bt");CMD("bt","on");assert(enables==1&&key_callback&&mouse_callback&&connects==0);
 CMD("bt","scan");assert(scans==1&&scan_duration==10000&&connects==0);
 BAD("bt","scan","0");BAD("bt","scan","60001");BAD("bt","scan","42949672960");BAD("bt","scan","-1");
 CMD("bt","scan","60000");assert(scan_duration==60000);
 CMD("bt","connect","01:02:03:04:05:06","random","justworks");
 assert(connects==1&&peer[0]==6&&peer[5]==1&&peer_type==1&&justworks);
 CMD("bt","connect","01:02:03:04:05:06","public");assert(!justworks&&peer_type==0);
 BAD("bt","connect","01:02:03:04:05:0Z","public");BAD("bt","connect","01:02:03:04:05:06xx","public");
 BAD("bt","connect","00:00:00:00:00:00","public");BAD("bt","connect","ff:ff:ff:ff:ff:ff","public");
 BAD("bt","connect","01:02:03:04:05:06","public","yes");assert(connects==2);
 status.pairing_action=2;BAD("bt","pair","accept");BAD("bt","pair","accept","12345");
 BAD("bt","pair","accept","12345x");assert(!pairs);
 BAD("bt","pair","accept","000123","challenge=8");assert(!pairs);
 CMD("bt","pair","accept","000123","challenge=9");assert(accepted&&pair_passkey==123&&pairs==1);
 status.pairing_action=4;status.comparison_number=123456;
 BAD("bt","pair","accept","123456");BAD("bt","pair","accept");
 CMD("bt","pair","accept","challenge=9");assert(accepted&&pairs==2);
 CMD("bt","pair","reject");assert(!accepted&&pairs==3);
 CMD("bt","status");assert(strstr(output_text,"123456")&&strstr(output_text,"bonds=RAM"));
 CMD("bt","devices");assert(strstr(output_text,"01:02:03:04:05:06 random"));
 CMD("bt","disconnect");CMD("bt","forget");assert(disconnects==1&&forgets==1);
 /* Actual existing decoder feeds the same IME path as USB input. */
 key_callback(NULL,0xe1,true);key_callback(NULL,4,true);key_callback(NULL,4,false);key_callback(NULL,0xe1,false);
 assert(!strcmp(typed,"A"));key_callback(NULL,HID_USAGE_CAPSLOCK,true);key_callback(NULL,4,true);assert(!strcmp(typed,"AA"));
 key_callback(NULL,0xe0,true);key_callback(NULL,6,true);key_callback(NULL,0xe0,false);assert(typed[2]==3);
 /* GUI receives no automatic mouse injection: explicit routing is required. */
 front_window=1;mouse_callback(NULL,1,1,1,0);assert(touches==0);
 CMD("bt","pointer","gui");mouse_callback(NULL,1,1,1,0);assert(touches==0); /* Already held: no synthetic new click. */
 mouse_callback(NULL,0,0,0,0);mouse_callback(NULL,1,32767,32767,0);
 assert(touches==1&&kinds[0]==GUI_TOUCH_DOWN&&touch_x==GUI_SCREEN_W-1&&touch_y==GUI_SCREEN_H-1);
 mouse_callback(NULL,1,-32768,-32768,0);assert(kinds[1]==GUI_TOUCH_MOVE&&touch_x==0&&touch_y==0);
 front_window=2;mouse_callback(NULL,1,1,1,0);assert(touches==3&&kinds[2]==GUI_TOUCH_UP);
 mouse_callback(NULL,1,1,1,0);assert(touches==3); /* No inherited drag into a different window. */
 mouse_callback(NULL,0,0,0,0);CMD("bt","pointer","console");
 mouse_callback(NULL,0,0,0,2);assert(scrolled==6);paused=true;mouse_callback(NULL,0,0,0,2);assert(scrolled==6);
 CMD("bt","pointer","off");paused=false;mouse_callback(NULL,0,0,0,2);assert(scrolled==6);
 puts("Bluetooth CLI parsing/consent/input-routing tests PASS");
}
