#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <m5os/mutex.h>
static CRITICAL_SECTION guard;
static _Thread_local task_t self;
static volatile LONG locks;
static mutex_t *active;
static bool boot;
bool sched_started(void) {return !boot;}
task_t *task_current(void) {return &self;}
void mutex_lock(mutex_t *m) {EnterCriticalSection(&guard);assert(!m->owner);m->owner=&self;active=m;InterlockedIncrement(&locks);}
void mutex_unlock(mutex_t *m) {assert(m->owner==&self);m->owner=NULL;active=NULL;LeaveCriticalSection(&guard);}
void fbcon_scroll_live(void) {}
#define IME_HOST_PLATFORM_PROVIDED 1
#define main original_ime_main
#define kconsole_inject_n original_inject
#include "../../tools/ime/host_ime_test.c"
#undef main
#undef kconsole_inject_n
size_t kconsole_inject_n(const char *text,size_t n) {
    assert(!boot && active && active->owner==&self);Sleep(0);return original_inject(text,n);
}
static DWORD WINAPI worker(void *arg) {
    const char *text=arg;
    for(unsigned i=0;i<100;i++) {
        ime_init();ime_feed(text);(void)ime_active();assert(ime_status());
        ime_set_anchor(1,2);ime_clear_anchor();
    }
    return 0;
}
int main(void) {
    InitializeCriticalSection(&guard);
    boot=true;ime_init();assert(locks==0);boot=false;
    char *args[]={"ime-test",NULL};assert(original_ime_main(1,args)==0);
    ime_bypass(true);s_outn=0;s_out[0]=0;
    HANDLE threads[4];const char *texts[]={"AAA","BBB","CCC","DDD"};
    for(unsigned i=0;i<4;i++)threads[i]=CreateThread(NULL,0,worker,(void *)texts[i],0,NULL);
    assert(WaitForMultipleObjects(4,threads,TRUE,10000)==WAIT_OBJECT_0);
    for(unsigned i=0;i<4;i++)CloseHandle(threads[i]);
    assert(s_outn==1200);
    for(unsigned i=0;i<s_outn;i+=3)assert(s_out[i]==s_out[i+1]&&s_out[i]==s_out[i+2]);
    assert(locks>=2400);
    ime_bypass(false);ime_feed(NULL);ime_feed("");
    DeleteCriticalSection(&guard);
    puts("PASS IME concurrent public operations, atomic input chunks, boot init, Japanese conversion regression");
    return 0;
}
