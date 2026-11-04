#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "../drivers/fpga/fpga_dev.c"
static bool permission=true,unsafe_close,fail_draw;static unsigned opens,closes,commands,allocations;
static device_t *registered;
bool task_has_perm(uint32_t p){assert(p==APP_PERM_DISPLAY);return permission;}
uint64_t hal_time_now_us(void){static uint64_t t;return ++t;}
void *kmalloc_psram(size_t n){void*p=calloc(1,n);if(p)allocations++;return p;}
void kfree(void*p){if(p){allocations--;free(p);}}
int device_register(device_t*d){registered=d;return 0;}
int fpga_parallel_open(fpga_parallel_t*b,const char*p,uint32_t hz){assert(!strcmp(p,"dock-jumper-v0")&&hz==1000000);opens++;b->owner=1;b->native=(void*)1;return 0;}
void fpga_parallel_close(fpga_parallel_t*b){closes++;if(!unsafe_close){b->owner=0;b->native=NULL;b->stream=NULL;b->owner_guarded=false;}}
int fpga_parallel_continuous(fpga_parallel_t*b){assert(b->tagged);b->stream=(void*)1;return 0;}
const fpga_wire_ops_t fpga_parallel_wire_ops={0},fpga_parallel_stream_wire_ops={0};
int fpga_wire_init(fpga_wire_t*w,const fpga_wire_ops_t*o,void*c,fpga_wire_workspace_t*ws){w->ops=o;w->context=c;w->workspace=ws;return 0;}
int fpga_wire_caps(fpga_wire_t*w,uint32_t t){assert(t==500000);w->sync_capable=true;return 0;}
int fpga_wire_tile_caps(fpga_wire_t*w,uint32_t t){assert(w->sync_capable&&t==500000);w->tile_capable=true;return 0;}
int fpga_wire_transport(fpga_wire_t*w,const fpga_wire_ops_t*o,void*c){w->ops=o;w->context=c;return 0;}
int fpga_wire_command(fpga_wire_t*w,uint8_t op,uint8_t a,uint8_t b,bool tagged,uint8_t*out,size_t n,uint32_t t){assert(w&&tagged&&n==8&&t==500000);commands++;memset(out,0,8);if(op==FRACTAL_INFO){out[4]=23;out[5]=1;}else if(op==FRACTAL_CYCLES_LO){out[4]=0x34;out[5]=0x12;}else if(op==FRACTAL_CYCLES_HI){out[4]=0xcd;out[5]=0xab;}else if(op==FRACTAL_DRAW){if(fail_draw)return FPGA_LINK_IO;}else{out[4]=a;out[5]=b;}return 0;}
int fpga_wire_tile(fpga_wire_t*w,unsigned plane,bool tag,uint8_t*out,uint32_t t){assert(w->tile_capable&&plane==0&&tag&&t==500000);for(unsigned i=0;i<1024;i++){uint16_t v=(uint16_t)(i^0x1234);out[8+2*i]=(uint8_t)v;out[9+2*i]=(uint8_t)(v>>8);}return 0;}
int main(void){
 fpga_dev_register();assert(registered&&registered->required_perm==APP_PERM_DISPLAY&&!strcmp(registered->name,"fpga"));
 uint16_t pixels[1024];for(unsigned i=0;i<1024;i++)pixels[i]=0xbeef;
 fpga_fractal_request_t q={.version=1,.size=sizeof q,.x0=-8192,.y0=-4096,.dx=256,.dy=256,.iterations=64,.mode=2,.pixels=pixels,.pixel_count=1024};
 permission=false;assert(gpu_ioctl(&dev,FPGA_IOC_FRACTAL_RENDER,&q)==ERR_PERM&&opens==0);permission=true;
 q.version=2;assert(gpu_ioctl(&dev,FPGA_IOC_FRACTAL_RENDER,&q)==ERR_INVAL&&opens==0);q.version=1;
 q.dx=0;assert(gpu_ioctl(&dev,FPGA_IOC_FRACTAL_RENDER,&q)==ERR_INVAL);q.dx=256;
 assert(gpu_ioctl(&dev,FPGA_IOC_FRACTAL_RENDER,&q)==0);assert(opens==1&&closes==1&&allocations==0&&q.cycles_valid&&q.engine_cycles==0xabcd1234u&&q.elapsed_us);
 for(unsigned i=0;i<1024;i++)assert(pixels[i]==(uint16_t)(i^0x1234));
 for(unsigned i=0;i<1024;i++)pixels[i]=0xbeef;
 fail_draw=true;assert(gpu_ioctl(&dev,FPGA_IOC_FRACTAL_RENDER,&q)==ERR_IO);assert(!q.cycles_valid&&q.elapsed_us==0&&allocations==0);for(unsigned i=0;i<1024;i++)assert(pixels[i]==0xbeef);fail_draw=false;
 unsafe_close=true;assert(gpu_ioctl(&dev,FPGA_IOC_FRACTAL_RENDER,&q)==ERR_IO&&allocations==1&&active_session);for(unsigned i=0;i<1024;i++)assert(pixels[i]==0xbeef);
 unsigned prior=opens;assert(gpu_ioctl(&dev,FPGA_IOC_FRACTAL_RENDER,&q)==ERR_BUSY&&opens==prior);
 kfree(active_session);active_session=NULL;assert(allocations==0&&commands);
 puts("FPGA device: permission/ABI/range checks, canonical command routing, actual tile-only commit, failure no-copy, complete close and quarantine retention passed");return 0;
}
