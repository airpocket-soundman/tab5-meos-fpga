#include <assert.h>
#include <stdio.h>
#include <m5os/task.h>
#include <m5os/kprintf.h>
static task_t a={.id=1,.state=TASK_RUNNING},b={.id=2,.state=TASK_RUNNING};
static task_t *self=&a;static int serial=-1;static unsigned irq_depth;
#define CONSOLE_IN_SIZE 16
static char s_in[CONSOLE_IN_SIZE];static unsigned s_in_head,s_in_tail;
uint32_t arch_irq_disable(void){return irq_depth++;}
void arch_irq_restore(uint32_t f){irq_depth=f;}
task_t *task_current(void){return self;}
int task_snapshot(task_info_t*out,int max){assert(max>=2);out[0]=(task_info_t){.id=a.id,.state=a.state};out[1]=(task_info_t){.id=b.id,.state=b.state};return 2;}
int hal_console_getc(void){assert(irq_depth);int c=serial;serial=-1;return c;}
/* ACTUAL_FOCUS */
#include "drivers/uart/uart_dev.c"
void task_sleep(uint32_t ms){(void)ms;}
uint64_t hal_time_now_us(void){static uint64_t t;return ++t;}
void kconsole_write(const char*s,size_t n){(void)s;(void)n;}
int device_register(device_t*d){assert(d->required_perm==APP_PERM_CONSOLE);return 0;}
static unsigned activity;
static void input_activity(void){assert(!irq_depth);activity++;}
int main(void){
 uart_dev_register();bool on=true,off=false;int32_t timeout=17;
 assert(!uart_ioctl(NULL,UART_IOC_SET_RX_TIMEOUT,&timeout));
 assert(!uart_ioctl(NULL,UART_IOC_FOCUS,&on)&&kconsole_get_focus()==1);
 self=&b;serial='x';assert(kconsole_getc()==-1&&serial=='x');assert(uart_ioctl(NULL,UART_IOC_FOCUS,&on)==ERR_BUSY);assert(uart_ioctl(NULL,UART_IOC_SET_RX_TIMEOUT,&timeout)==ERR_BUSY);assert(uart_ioctl(NULL,UART_IOC_POLL_CHAR,&timeout)==ERR_BUSY&&serial=='x');assert(!uart_close(NULL)&&kconsole_get_focus()==1);
 self=&a;uint8_t polled=0;assert(uart_ioctl(NULL,UART_IOC_POLL_CHAR,&polled)==1&&polled=='x'&&s_rx_timeout_ms==17);
 assert(uart_ioctl(NULL,UART_IOC_POLL_CHAR,&polled)==0&&s_rx_timeout_ms==17);
 assert(uart_ioctl(NULL,UART_IOC_POLL_CHAR,NULL)==ERR_INVAL);timeout=0;assert(!uart_ioctl(NULL,UART_IOC_SET_RX_TIMEOUT,&timeout));assert(!uart_close(NULL)&&kconsole_get_focus()==0&&s_rx_timeout_ms==17);
 self=&b;kconsole_set_focus(2);self=&a;assert(uart_ioctl(NULL,UART_IOC_FOCUS,&on)==ERR_BUSY&&kconsole_get_focus()==2);
 b.state=TASK_TERMINATED;assert(!uart_ioctl(NULL,UART_IOC_FOCUS,&on)&&kconsole_get_focus()==1);b.state=TASK_RUNNING;
 a.state=TASK_TERMINATED;self=&b;serial='q';assert(kconsole_getc()=='q'&&kconsole_get_focus()==0);assert(!uart_close(NULL)&&!focus_owner);a.state=TASK_RUNNING;
 self=&a;assert(!uart_ioctl(NULL,UART_IOC_FOCUS,&on));a.id=3;self=&b;assert(kconsole_get_focus()==0&&!uart_close(NULL)&&!focus_owner);
 self=&a;kconsole_set_focus(3);assert(!uart_ioctl(NULL,UART_IOC_FOCUS,&on));assert(!uart_ioctl(NULL,UART_IOC_FOCUS,&off)&&kconsole_get_focus()==3); /* Restore preexisting same-task focus. */
 kconsole_set_focus(0);assert(!irq_depth);
 kconsole_set_input_hook(input_activity);
 serial='s';assert(kconsole_getc()=='s'&&activity==1);
 assert(kconsole_getc()==-1&&activity==1);
 s_in[s_in_head]='i';s_in_head=(s_in_head+1)%CONSOLE_IN_SIZE;
 assert(kconsole_getc()=='i'&&activity==1); /* Injected keys have their own wake filter. */
 kconsole_set_focus(a.id);assert(kconsole_suspend_busy());a.state=TASK_TERMINATED;
 assert(!kconsole_suspend_busy());a.state=TASK_RUNNING;

 puts("UART focus: exclusive input, atomic non-stealing claim, foreign close, prior-focus/timeout restore, killed owner and TCB reuse auto-reap passed");return 0;
}
