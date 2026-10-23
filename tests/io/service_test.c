#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <m5os/io.h>
#include <m5os/io_i2c.h>
#include <m5os/io_spi.h>
#include <m5os/io_uart.h>
#include <m5os/hal_io_analog.h>
#include <m5os/mutex.h>
static task_t current={.id=42};static bool alive42=true,alive43=true;
task_t *task_current(void){return &current;}
int task_snapshot(task_info_t *out,int max){int n=0;if(alive42&&n<max)out[n++]=(task_info_t){.id=42,.state=TASK_RUNNING};if(alive43&&n<max)out[n++]=(task_info_t){.id=43,.state=TASK_RUNNING};return n;}
void mutex_lock(mutex_t *m){assert(!m->owner);m->owner=&current;}
void mutex_unlock(mutex_t *m){assert(m->owner==&current);m->owner=NULL;}
static int levels[55],modes[55],writes,cleanup;static jmp_buf killed;static bool kill_close;
void hal_io_gpio_input(int p,int pull){assert(p>=0&&p<55);modes[p]=0;if(pull)levels[p]=pull==1;writes++;}
void hal_io_gpio_output(int p,int value){modes[p]=1;levels[p]=value;writes++;}
void hal_io_gpio_release(int p){hal_io_gpio_input(p,0);}
int hal_gpio_read(int p){return levels[p];}
void hal_gpio_write(int p,int v){assert(modes[p]==1);levels[p]=v;writes++;}
int hal_io_adc_read_raw(int p,unsigned attenuation,uint16_t *raw){(void)p;assert(attenuation<4);*raw=1234;return 0;}
int hal_io_pwm_set(int p,uint32_t hz,unsigned duty,uint32_t *actual){(void)p;if(!hz||duty>1000||!actual)return ERR_INVAL;*actual=hz;return 0;}
int hal_io_pwm_stop(int p){(void)p;return 0;}
void io_i2c_release_owner(io_owner_t o){(void)o;cleanup++;if(kill_close){kill_close=false;longjmp(killed,1);}}
void io_spi_release_owner(io_owner_t o){(void)o;cleanup++;}
void io_uart_release_owner(io_owner_t o){(void)o;cleanup++;}
static uint64_t clock_us;uint64_t hal_time_now_us(void){return clock_us+=100;}
static unsigned transfers;
int io_i2c_open(io_owner_t o,const io_i2c_config_t*c,uint32_t*h){int p[]={c->sda,c->scl};int r=io_pins_claim(o,p,2,IO_USE_I2C);if(!r)*h=42;return r;}
int io_i2c_close(io_owner_t o,uint32_t h){(void)o;(void)h;return ERR_BADF;}
int io_i2c_transfer(io_owner_t o,uint32_t h,uint8_t a,const uint8_t*t,size_t tn,uint8_t*r,size_t rn){(void)o;(void)h;(void)a;(void)t;(void)tn;transfers++;if(r)memset(r,0x5a,rn);return 0;}
int io_spi_open(io_owner_t o,const io_spi_config_t*c,uint32_t*h){(void)o;(void)c;(void)h;return ERR_NOSYS;}
int io_spi_close(io_owner_t o,uint32_t h){(void)o;(void)h;return ERR_BADF;}
int io_spi_transfer(io_owner_t o,uint32_t h,const uint8_t*t,uint8_t*r,size_t n){(void)o;(void)h;(void)t;(void)r;(void)n;return ERR_NOSYS;}
int io_uart_selftest(void){return 0;}
int io_uart_open(io_owner_t o,const io_uart_config_t*c,uint32_t*h){(void)o;(void)c;(void)h;return ERR_NOSYS;}
int io_uart_close(io_owner_t o,uint32_t h){(void)o;(void)h;return ERR_BADF;}
int io_uart_read(io_owner_t o,uint32_t h,void*b,size_t n,uint32_t t){(void)o;(void)h;(void)b;(void)n;(void)t;return ERR_TIMEOUT;}
int io_uart_write(io_owner_t o,uint32_t h,const void*b,size_t n,uint32_t t){(void)o;(void)h;(void)b;(void)n;(void)t;return ERR_TIMEOUT;}
int io_uart_flush(io_owner_t o,uint32_t h,uint32_t t){(void)o;(void)h;(void)t;return ERR_TIMEOUT;}
int ksnprintf(char *b,size_t cap,const char *fmt,...);
#include <stdarg.h>
int ksnprintf(char *b,size_t cap,const char *fmt,...){va_list a;va_start(a,fmt);int n=vsnprintf(b,cap,fmt,a);va_end(a);return n;}
#include "../../drivers/io/io.c"
#include "../../user/io/io_app.c"
static char output[8192];static void collect(const char*s){assert(strlen(output)+strlen(s)<sizeof output);strcat(output,s);}
static int run(int argc,char **argv){output[0]=0;return io_app_command(argc,argv,collect);}
#define RUN(...) run((int)(sizeof((char*[]){__VA_ARGS__})/sizeof(char*)),(char*[]){__VA_ARGS__})
int main(void){
 io_owner_t a,b;assert(!io_owner_create("A",&a));assert(!io_owner_create("B",&b));
 int p[]={2,31};int before=writes;assert(io_pins_claim(a,p,2,IO_USE_DIGITAL)==ERR_PERM);io_pin_info_t info;io_pin_info(2,&info);assert(!info.owner&&writes==before);
 int dup[]={2,2};assert(io_pins_claim(a,dup,2,IO_USE_DIGITAL)==ERR_INVAL);
 int pin=2;assert(!io_pins_claim(a,&pin,1,IO_USE_DIGITAL));assert(io_pins_claim(a,&pin,1,IO_USE_DIGITAL)==ERR_BUSY);assert(io_gpio_write(a,2,1)==ERR_PERM);
 assert(!io_gpio_config(a,2,true,1,IO_PULL_NONE));assert(levels[2]==1);assert(io_gpio_write(b,2,0)==ERR_PERM);assert(!io_gpio_write(a,2,0));
 assert(!io_gpio_config(a,2,false,0,IO_PULL_UP));int value;assert(!io_gpio_read(a,2,&value)&&value==1);assert(io_gpio_write(a,2,0)==ERR_PERM);
 assert(io_pins_release(b,&pin,1)==ERR_PERM);assert(!io_pins_release(a,&pin,1));assert(!io_owner_close(a));assert(!io_owner_close(b));assert(!io_owner_valid(a));
 pin=0;assert(!io_owner_create("keyboard-test",&a));assert(io_pins_claim(a,&pin,1,IO_USE_DIGITAL)==ERR_PERM);io_tab5_keyboard_detected(false);assert(!io_pins_claim(a,&pin,1,IO_USE_DIGITAL));assert(!io_owner_close(a));
 assert(!io_owner_create("close-kill",&a));pin=3;assert(!io_pins_claim(a,&pin,1,IO_USE_DIGITAL));current.id=43;
 if(!setjmp(killed)){kill_close=true;io_owner_close(a);assert(0);}
 assert(!io_owner_valid(a));alive43=false;current.id=42;io_reap_dead_owners();assert(!io_owner_valid(a));io_pin_info(pin,&info);assert(!info.owner);alive43=true;
 assert(!io_owner_create("dead-vm",&a));pin=4;assert(!io_pins_claim(a,&pin,1,IO_USE_DIGITAL));alive42=false;current.id=43;io_reap_dead_owners();io_pin_info(pin,&info);assert(!info.owner);alive42=true;current.id=42;
 assert(RUN("gpio","mode","31","out","1")==1);assert(RUN("gpio","mode","2","out","1")==0);assert(RUN("gpio","read","2")==0&&strstr(output,"1\n"));
 assert(RUN("gpio","write","2","2")==1);assert(RUN("gpio","release","2")==0);assert(RUN("adc","read","16")==0&&strstr(output,"raw=1234"));
 before=transfers;assert(RUN("i2c","xfer","42","0x20","0","1")==1&&transfers==(unsigned)before);assert(RUN("i2c","open","31","32","100000")==1);
 assert(RUN("uart","open","4294967296","7","115200")==1);assert(RUN("io","help")==0);assert(RUN("io","reset")==0);assert(RUN("uart","selftest")==0);assert(io_app_command(0,NULL,NULL)==1);
 puts("IO ownership/atomic claims/recovery/CLI tests PASS");return 0;
}
