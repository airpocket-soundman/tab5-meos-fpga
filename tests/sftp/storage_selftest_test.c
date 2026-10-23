/* Reuse the engine suite's in-memory VFS; do not run its main here. */
#define main original_engine_test_main
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#include "server_test.c"
#pragma clang diagnostic pop
#undef main
#include <stdarg.h>
static uint64_t test_clock=100;
uint64_t hal_time_now_us(void){return test_clock++;}
int ksnprintf(char *b,size_t n,const char *format,...){va_list args;va_start(args,format);int r=vsnprintf(b,n,format,args);va_end(args);return r;}
void kprintf(const char *format,...){va_list args;va_start(args,format);vprintf(format,args);va_end(args);}
#include "../../user/ssh/sftp_selftest.c"
static void no_fds(void){for(unsigned i=0;i<16;i++)assert(!fds[i].used);}
int main(void){
    assert(!sftp_storage_selftest(0));assert(!sftp_storage_selftest("/usb"));
    assert(!opens && !mutations);
    allocation_fail=true;assert(!sftp_storage_selftest("/sd"));allocation_fail=false;
    assert(sftp_storage_selftest("/sd"));no_fds();
    file_t *f=find("/sd/sftp_00000064.bin");assert(f && f->size==8192);
    for(unsigned i=0;i<8192;i++)assert(f->data[i]==st_pattern(i));
    /* Force a filename collision: EXCL must leave the original bytes intact. */
    test_clock=100;assert(sftp_storage_selftest("/sd"));no_fds();
    assert(f->size==8192);for(unsigned i=0;i<8192;i++)assert(f->data[i]==st_pattern(i));
    close_fail=true;assert(!sftp_storage_selftest("/sd"));close_fail=false;no_fds();
    mounted=false;assert(!sftp_storage_selftest("/sd"));mounted=true;no_fds();
    puts("Local actual-engine SFTP 8192-byte storage selftest, collision safety, close failure cleanup PASS");
}
