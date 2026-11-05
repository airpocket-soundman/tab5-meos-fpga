#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <stdatomic.h>
#include <sched.h>
#include <unistd.h>
#endif
#include <assert.h>
#include <m5os/mutex.h>
#ifdef _WIN32
static CRITICAL_SECTION guard;
static volatile LONG locks, consume_wake;
#define LOCK() EnterCriticalSection(&guard)
#define UNLOCK() LeaveCriticalSection(&guard)
#define COUNT_LOCK() InterlockedIncrement(&locks)
#define TAKE_WAKE() InterlockedExchange(&consume_wake,0)
#define YIELD() Sleep(0)
#else
static pthread_mutex_t guard=PTHREAD_MUTEX_INITIALIZER;
static atomic_int locks,consume_wake;
#define LOCK() assert(pthread_mutex_lock(&guard)==0)
#define UNLOCK() assert(pthread_mutex_unlock(&guard)==0)
#define COUNT_LOCK() atomic_fetch_add(&locks,1)
#define TAKE_WAKE() atomic_exchange(&consume_wake,0)
#define YIELD() sched_yield()
#endif
static _Thread_local task_t self;
static mutex_t *active;
static bool boot;
bool sched_started(void) {return !boot;}
task_t *task_current(void) {return &self;}
void mutex_lock(mutex_t *m) {LOCK();assert(!m->owner);m->owner=&self;active=m;COUNT_LOCK();}
void mutex_unlock(mutex_t *m) {assert(m->owner==&self);m->owner=NULL;active=NULL;UNLOCK();}
void fbcon_scroll_live(void) {}
bool suspend_filter_input(void){return TAKE_WAKE()!=0;}
#define IME_HOST_PLATFORM_PROVIDED 1
#define main original_ime_main
#define kconsole_inject_n original_inject
#include "../../tools/ime/host_ime_test.c"
#undef main
#undef kconsole_inject_n
size_t kconsole_inject_n(const char *text,size_t n) {
    assert(!boot && active && active->owner==&self);YIELD();return original_inject(text,n);
}
#ifdef _WIN32
static DWORD WINAPI worker(void *arg) {
#else
static void *worker(void *arg) {
#endif
    const char *text=arg;
    for(unsigned i=0;i<100;i++) {
        ime_init();ime_feed(text);(void)ime_active();assert(ime_status());
        ime_set_anchor(1,2);ime_clear_anchor();
    }
    return 0;
}
int main(void) {
    #ifdef _WIN32
    InitializeCriticalSection(&guard);
    #else
    alarm(10); /* Bound a deadlock regression on the CI host. */
    #endif
    boot=true;ime_init();assert(locks==0);boot=false;
    char *args[]={"ime-test",NULL};assert(original_ime_main(1,args)==0);
    ime_bypass(true);s_outn=0;s_out[0]=0;
    consume_wake=1;ime_feed("WAKE");assert(s_outn==0);
    const char *texts[]={"AAA","BBB","CCC","DDD"};
    #ifdef _WIN32
    HANDLE threads[4];
    for(unsigned i=0;i<4;i++)threads[i]=CreateThread(NULL,0,worker,(void *)texts[i],0,NULL);
    assert(WaitForMultipleObjects(4,threads,TRUE,10000)==WAIT_OBJECT_0);
    for(unsigned i=0;i<4;i++)CloseHandle(threads[i]);
    #else
    pthread_t threads[4];
    for(unsigned i=0;i<4;i++)assert(pthread_create(&threads[i],NULL,worker,(void *)texts[i])==0);
    for(unsigned i=0;i<4;i++)assert(pthread_join(threads[i],NULL)==0);
    alarm(0);
    #endif
    assert(s_outn==1200);
    for(unsigned i=0;i<s_outn;i+=3)assert(s_out[i]==s_out[i+1]&&s_out[i]==s_out[i+2]);
    assert(locks>=2400);
    ime_bypass(false);ime_feed(NULL);ime_feed("");
    #ifdef _WIN32
    DeleteCriticalSection(&guard);
    #else
    assert(pthread_mutex_destroy(&guard)==0);
    #endif
    puts("PASS IME concurrent public operations, atomic input chunks, boot init, Japanese conversion regression");
    return 0;
}
