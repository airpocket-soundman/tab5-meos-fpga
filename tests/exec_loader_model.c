#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
/* ACTUAL_LOADER */
#include "kernel/lib/crc32.c"
const m5_api_t g_m5_api={0};
static app_t app={.app_id=17};static void *blocks[2];static size_t sizes[2];
static unsigned allocations,launches,aborts,fail_alloc;static bool alias_allowed=true,sync_allowed=true;
app_t *app_reserve(const char*n,uint32_t p){assert(!strcmp(n,"hello")&&p==0);return &app;}
void *kmalloc_internal_owned(size_t n,uint32_t id){assert(id==17);unsigned k=allocations++;if(fail_alloc==allocations)return NULL;assert(k<2);sizes[k]=n;return blocks[k]=malloc(n);}
void app_abort(app_t*a){assert(a==&app);aborts++;for(unsigned i=0;i<2;i++){free(blocks[i]);blocks[i]=NULL;}}
int app_launch(app_t*a,void(*entry)(void*),void*ctx,uint8_t priority,size_t stack){assert(a==&app&&entry&&ctx&&priority==TASK_PRIO_DEFAULT&&stack==2048);launches++;return 0;}
uintptr_t hal_exec_alias(const void*p,size_t n){assert(n==260&&((uintptr_t)p%128)==0);return alias_allowed?(uintptr_t)p:0;}
bool hal_exec_sync(void*p,size_t n){assert(((uintptr_t)p%128)==0&&n==260);assert((uintptr_t)p>=(uintptr_t)blocks[0]&&((uintptr_t)p+383u)/128u*128u<=(uintptr_t)blocks[0]+sizes[0]);return sync_allowed;}
static void reset(void){allocations=launches=aborts=fail_alloc=0;alias_allowed=sync_allowed=true;}
int main(void){
 uint8_t file[64+260]={0};m5ex_header_t h={.magic={'M','5','E','X'},.version=1,.header_size=64,.flags=M5EX_ARCH_THIS,.api_version=1,.image_size=260,.mem_size=260,.code_size=260,.stack_size=2048,.name="hello"};h.image_crc32=crc32(file+64,260);memcpy(file,&h,64);
 reset();alias_allowed=false;assert(app_start_image(file,sizeof file,NULL)==ERR_NOSYS&&aborts==1&&!launches&&!blocks[0]&&!blocks[1]);
 reset();sync_allowed=false;assert(app_start_image(file,sizeof file,NULL)==ERR_IO&&aborts==1&&!launches&&!blocks[0]&&!blocks[1]);
 reset();fail_alloc=2;assert(app_start_image(file,sizeof file,NULL)==ERR_NOMEM&&aborts==1&&!launches&&!blocks[0]);
 reset();uint32_t id=0;assert(!app_start_image(file,sizeof file,&id)&&launches==1&&id==17&&!aborts);app_abort(&app);
 puts("Actual loader: NX refusal/cache failure/allocation failure abort and reclaim before launch; aligned owned image success passed");return 0;
}
