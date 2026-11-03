#include "../user/gfxbench/gfxbench_app.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" {
#include <m5os/mutex.h>
}
static uint64_t time_us;
static unsigned allocations,outstanding,fail_at,draws,polls;
static bool lcd_fail,paused,busy,early_cancel,controls;
extern "C" {
void *kmalloc(size_t n) {if(++allocations==fail_at)return nullptr;void *p=std::malloc(n);if(p)outstanding++;return p;}
void *kmalloc_psram(size_t n) {return kmalloc(n);}
void kfree(void *p) {if(p){assert(outstanding);outstanding--;std::free(p);}}
uint64_t hal_time_now_us() {return time_us+=100;}
void task_sleep(uint32_t ms) {time_us+=uint64_t(ms)*1000;}
bool mutex_trylock(mutex_t *m) {if(busy||m->owner)return false;m->owner=reinterpret_cast<task_t *>(uintptr_t(1));return true;}
void mutex_unlock(mutex_t *m) {assert(m->owner);m->owner=nullptr;}
bool fbcon_is_paused() {return paused;}
void fbcon_pause(bool p) {paused=p;}
int dq_pollkey() {
    polls++;
    if(controls) {
        const int keys[]={'l','r','n',' ',' ','b'};
        if(polls<=6)return keys[polls-1];
        if(polls>=151)return 'q';
    }
    return early_cancel&&polls>=3?3:-1;
}
void display_clear(uint16_t) {}
void display_fill_rect(int,int,int,int,uint16_t) {}
void display_draw_text(int,int,const char *,uint16_t,uint16_t,int) {}
bool display_begin_frame() {return true;}
bool display_end_frame() {return !lcd_fail;}
void display_draw_bitmap_scaled(int x,int y,int w,int h,const uint16_t *p,int scale)
{assert(x>=0&&y>=0&&w==640&&h==360&&p&&scale>0);draws++;}
void kprintf(const char *fmt,...) {va_list a;va_start(a,fmt);std::vprintf(fmt,a);va_end(a);}
int ksnprintf(char *p,size_t n,const char *fmt,...) {va_list a;va_start(a,fmt);int r=std::vsnprintf(p,n,fmt,a);va_end(a);return r;}
}
int main(int argc,char **argv)
{
    if(argc>1&&!std::strncmp(argv[1],"--test-fail-",12)){fail_at=unsigned(std::atoi(argv[1]+12));argc--;argv++;}
    else if(argc>1&&!std::strcmp(argv[1],"--test-lcd-fail")){lcd_fail=true;argc--;argv++;}
    else if(argc>1&&!std::strcmp(argv[1],"--test-cancel")){early_cancel=true;argc--;argv++;}
    else if(argc>1&&!std::strcmp(argv[1],"--test-busy")){busy=true;argc--;argv++;}
    else if(argc>1&&!std::strcmp(argv[1],"--test-paused")){paused=true;argc--;argv++;}
    else if(argc>1&&!std::strcmp(argv[1],"--test-controls")){controls=true;argc--;argv++;}
    bool before=paused;
    int result=gfxbench_cmd_main(argc-1,argv+1);
    assert(!outstanding&&paused==before);
    if(fail_at&&fail_at<=3)assert(!draws);
    return result;
}
