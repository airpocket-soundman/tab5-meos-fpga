#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../user/sl/sl_app.c"
static uint64_t now;
static int geometry_rows=24,geometry_cols=80;
static unsigned draws,frames,allocs,frees,alt_in,alt_out;
static bool allocation_fail,busy;
static int injected=-1;
static unsigned slow_us;
static uint8_t *allocation;static size_t allocation_size;
static char escape[40];static unsigned escape_n;
static bool in_escape,screen;
uint64_t hal_time_now_us(void){return now;}
void task_sleep(uint32_t ms){now+=(uint64_t)ms*1000;}
void task_yield(void){}
bool mutex_trylock(mutex_t *m){(void)m;if(busy)return false;busy=true;return true;}
void mutex_unlock(mutex_t *m){(void)m;assert(busy);busy=false;}
void fbcon_get_geometry(int *r,int *c,int *a,int *b){*r=geometry_rows;*c=geometry_cols;(void)a;(void)b;}
void *kmalloc_psram(size_t n){
    if(allocation_fail)return 0;
    assert(!allocation);allocation=malloc(n+64);assert(allocation);allocation_size=n;
    memset(allocation,0xa5,32);memset(allocation+32+n,0x5a,32);allocs++;return allocation+32;
}
void kfree(void *p){
    assert(p==allocation+32);
    sl_state_t *s=p;assert(s->smoke_count>=0 && s->smoke_count<=SL_SMOKE_MAX);
    for(int i=0;i<s->smoke_count;i++){assert(s->smoke[i].ptrn>=0 && s->smoke[i].ptrn<16);assert(s->smoke[i].kind==0 || s->smoke[i].kind==1);}
    for(unsigned i=0;i<32;i++){assert(allocation[i]==0xa5);assert(allocation[32+allocation_size+i]==0x5a);}
    free(allocation);allocation=0;frees++;
}
int kconsole_getc(void){int value=injected;injected=-1;return value;}
int ksnprintf(char *b,size_t cap,const char *f,...){va_list a;va_start(a,f);int n=vsnprintf(b,cap,f,a);va_end(a);return n;}
static void validate_escape(void){
    escape[escape_n]=0;
    if(!strcmp(escape,"[?1049h")){assert(!screen);screen=true;alt_in++;}
    else if(!strcmp(escape,"[?1049l")){assert(screen);screen=false;alt_out++;}
    else if(escape_n && escape[escape_n-1]=='H'){
        unsigned row=1,col=1;
        if(strcmp(escape,"[H"))assert(sscanf(escape,"[%u;%uH",&row,&col)==2);
        assert(row>=1 && row<=(unsigned)sl_rows && col>=1 && col<=(unsigned)sl_columns);
        frames++;
    }
}
void kconsole_write(const char *p,size_t n){
    now+=slow_us;
    for(size_t i=0;i<n;i++){
        unsigned char c=p[i];
        if(in_escape){
            assert(escape_n+1<sizeof escape);escape[escape_n++]=c;
            if(escape_n>1 && c>=0x40 && c<=0x7e){validate_escape();in_escape=false;escape_n=0;}
        }else if(c==27){in_escape=true;escape_n=0;}
        else if(screen && c!=' ' && c!='\n' && c!='\r')draws++;
    }
}
void kputs(const char *s){kconsole_write(s,strlen(s));}
static void run(char *option){char *v[]={"sl",option};assert(sl_cmd_main(option?2:1,v)==0);assert(!screen && !allocation && !busy && !sl_context);}
int main(void){
    run(0);assert(draws && frames && alt_in==1 && alt_out==1);
    for(unsigned i=0;i<30;i++){run("-a");run("-lF");run("-cFa");}
    assert(!sl_accident || !sl_logo); /* options are overwritten, not accumulated */
    geometry_rows=1;geometry_cols=1;run("-cFa");
    geometry_rows=80;geometry_cols=160;run("-aF");
    geometry_rows=1000;geometry_cols=1000;run("-l");assert(sl_rows==80 && sl_columns==160);
    geometry_rows=24;geometry_cols=80;
    allocation_fail=true;char *v[]={"sl"};assert(sl_cmd_main(1,v)==1);allocation_fail=false;
    unsigned count=allocs;char *bad[]={"sl","-z"};assert(sl_cmd_main(2,bad)==1 && allocs==count);
    char *help[]={"sl","--help"};assert(!sl_cmd_main(2,help) && allocs==count);
    busy=true;assert(sl_cmd_main(1,v)==1);busy=false;
    injected=3;assert(sl_cmd_main(1,v)==130 && !screen && !allocation);
    injected=27;assert(sl_cmd_main(1,v)==130 && !screen && !allocation);
    slow_us=100000;assert(sl_cmd_main(1,v)==124 && !screen && !allocation);slow_us=0;
    run(0);assert(!sl_accident && !sl_logo && !sl_fly && !sl_c51);
    assert(allocs==frees && alt_in==alt_out && !screen);
    puts("Native original SL adaptation: variants/repeat/clip/smoke/cancel/deadline/cleanup PASS");
}
