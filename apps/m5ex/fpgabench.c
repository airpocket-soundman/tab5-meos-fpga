/* Fixed-workload device benchmark, loaded from SD; no LCD or raw GPIO. */
#include "../sdk/m5ex_sdk.h"
#include <m5os/dev_fpga.h>
#include <m5os/errno.h>
#include "../../third_party/carrier_link/fractal_reference.h"
M5EX_APP("fpgabench", M5_PERM_CONSOLE|M5_PERM_DISPLAY, 4096);
typedef struct {uint16_t expected[1024],received[1024];} buffers_t;
void app_main(const m5_api_t*a,void*arg)
{
    (void)arg;
    if(!a||a->version<M5_API_VERSION||a->size<offsetof(m5_api_t,snprintf)+sizeof a->snprintf)return;
    int console=a->open("/dev/uart",M5_O_WRITE),device=-1;
    buffers_t*b=a->alloc(sizeof*b);
    if(console<0)goto done;
    if(!b){const char *failure="{\"fpgabench\":true,\"error\":-7}\n";a->write(console,failure,strlen(failure));goto done;}
    device=a->open("/dev/fpga",M5_O_READ|M5_O_WRITE);
    if(device<0){char line[80];int n=a->snprintf(line,sizeof line,"{\"fpgabench\":true,\"error\":%d}\n",device);a->write(console,line,n);goto done;}
    for(unsigned scene=0;scene<2;scene++){
        fractal_config c={-10035,-4096,280,264,-2949,819,64,0};
        if(scene){c.x0=-4096;c.y0=-2048;c.dx=c.dy=132;c.julia=1;}
        fractal_reference_tile(&c,b->expected,NULL);
        for(unsigned repeat=0;repeat<3;repeat++){
            uint32_t elapsed=0,cycles=0,started=a->uptime_ms();unsigned matched=0,completed=0;
            int error=a->ioctl(device,FPGA_IOC_FRACTAL_BEGIN,NULL);
            bool held=error==0;if(error==ERR_NOSYS)error=0;
            for(unsigned i=0;i<16&&!error;i++){
                fpga_fractal_request_t r={.version=1,.size=sizeof r,.x0=c.x0,.y0=c.y0,
                    .cx=c.cx,.cy=c.cy,.dx=c.dx,.dy=c.dy,.iterations=64,.mode=c.julia,
                    .pixels=b->received,.pixel_count=1024};
                error=a->ioctl(device,FPGA_IOC_FRACTAL_RENDER,&r);
                if(!error){elapsed+=r.elapsed_us;cycles+=r.engine_cycles;completed++;
                    for(unsigned n=0;n<1024;n++){if(b->received[n]!=b->expected[n]){error=-6;break;}matched++;}}
            }
            if(held){int e=a->ioctl(device,FPGA_IOC_FRACTAL_END,NULL);if(!error&&e)error=e;}
            uint32_t wall=a->uptime_ms()-started;
            char line[256];int n=a->snprintf(line,sizeof line,
                "{\"fpgabench\":true,\"scene\":\"%s\",\"repeat\":%u,\"calls\":%u,\"matched\":%u,\"device_us\":%u,\"cycles\":%u,\"session\":%s,\"wall_ms\":%u,\"error\":%d}\n",
                scene?"julia":"mandelbrot",repeat,completed,matched,elapsed,cycles,held?"true":"false",wall,error);
            a->write(console,line,n);
            if(error)goto done;
        }
    }
done:
    if(device>=0)a->close(device);
    if(b)a->release(b);
    if(console>=0)a->close(console);
}
