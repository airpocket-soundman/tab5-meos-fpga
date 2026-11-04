/* Standalone loaded M5EX app. Not embedded into the firmware. */
#include "../sdk/m5ex_sdk.h"
#include <m5os/dev_display.h>
#include <m5os/dev_fpga.h>
#include <m5os/dev_uart.h>
#include "../../third_party/carrier_link/fractal_reference.h"

M5EX_APP("fractal", M5_PERM_CONSOLE|M5_PERM_DISPLAY, 4096);
#define PIXELS 1024u
#define MAX_SCALE 16u

typedef struct {
    uint16_t cpu[PIXELS],gpu[PIXELS];
    /* One expanded32-pixel source row, not a full512x512 framebuffer. */
    uint16_t strip[32u*MAX_SCALE*MAX_SCALE];
    fpga_fractal_request_t request;
} workspace_t;
static const fractal_config scenes[2]={
    {-10035,-4096,280,264,-2949,819,64,0},
    {-4096,-2048,132,132,-2949,819,64,1}
};
static int text(const m5_api_t*a,int d,int y,uint16_t color,const char*s)
{
    display_text_t t={.x=8,.y=(int16_t)y,.fg=color,.bg=COLOR_BLACK,.scale=1,.text=s};
    return a->ioctl(d,DISPLAY_IOC_DRAW_TEXT,&t);
}
static void milliseconds(const m5_api_t*a,char out[16],uint32_t value,bool measured)
{
    if(!measured){out[0]='-';out[1]=0;}
    else if(!value){out[0]='<';out[1]='1';out[2]=0;}
    else a->snprintf(out,16,"%u",value);
}
static int draw(const m5_api_t*a,int d,const display_info_t*info,
                workspace_t*w,const uint16_t*p,unsigned scale)
{
    unsigned width=32u*scale;
    for(unsigned y=0;y<32;y++){
        for(unsigned sy=0;sy<scale;sy++)for(unsigned x=0;x<width;x++)
            w->strip[sy*width+x]=p[y*32+x/scale];
        display_blit_t b={.x=(int16_t)((info->width-width)/2),
            .y=(int16_t)(64+y*scale),.w=(int16_t)width,.h=(int16_t)scale,.pixels=w->strip};
        int e=a->ioctl(d,DISPLAY_IOC_BLIT,&b);if(e<0)return e;
    }
    return 0;
}
static int gpu(const m5_api_t*a,int f,workspace_t*w,const fractal_config*c)
{
    fpga_fractal_request_t*r=&w->request;
    memset(r,0,sizeof*r);r->version=FPGA_FRACTAL_ABI_VERSION;r->size=sizeof*r;
    r->x0=c->x0;r->y0=c->y0;r->cx=c->cx;r->cy=c->cy;r->dx=c->dx;r->dy=c->dy;
    r->iterations=c->limit;r->mode=c->julia;r->pixels=w->gpu;r->pixel_count=PIXELS;
    return a->ioctl(f,FPGA_IOC_FRACTAL_RENDER,r);
}
void app_main(const m5_api_t*a,void*arg)
{
    (void)arg;
    int console=-1,display=-1,fpga=-1;
    workspace_t*w=NULL;bool paused=false,frame=false,focused=false;
    if(!a||a->version<M5_API_VERSION||a->size<offsetof(m5_api_t,snprintf)+sizeof a->snprintf)return;
    console=a->open("/dev/uart",M5_O_READ|M5_O_WRITE);
    if(console<0)goto done;
    bool focus=true;
    if(a->ioctl(console,UART_IOC_FOCUS,&focus)<0)goto done;
    focused=true;
    display=a->open("/dev/display",M5_O_WRITE);
    if(console<0||display<0)goto done;
    bool pause=true;
    if(a->ioctl(display,DISPLAY_IOC_CONSOLE_PAUSE,&pause)<0)goto done;
    paused=true;
    display_info_t info={0};
    if(a->ioctl(display,DISPLAY_IOC_GET_INFO,&info)<0||info.width<32||info.height<128)goto done;
    unsigned scale=info.width/32,vertical=(info.height-96u)/32;
    if(scale>vertical)scale=vertical;
    if(scale>MAX_SCALE)scale=MAX_SCALE;
    if(!scale)goto done;
    w=a->alloc(sizeof*w);if(!w)goto done;
    bool use_gpu=false,compare=false,redraw=true;unsigned scene=0;
    for(;;){
        if(redraw){
            const fractal_config*c=&scenes[scene];
            uint32_t cpu_ms=0,gpu_ms=0,draw_ms=0,work=0;int error=0;unsigned mismatch=PIXELS;
            if(!use_gpu||compare){uint32_t t=a->uptime_ms();
                if(!fractal_reference_tile(c,w->cpu,&work))error=-1;
                cpu_ms=a->uptime_ms()-t;
            }
            if(!error&&(use_gpu||compare)){
                if(fpga<0)fpga=a->open("/dev/fpga",M5_O_READ|M5_O_WRITE);
                uint32_t t=a->uptime_ms();error=fpga<0?fpga:gpu(a,fpga,w,c);gpu_ms=a->uptime_ms()-t;
            }
            if(!error&&compare){for(unsigned i=0;i<PIXELS;i++)if(w->cpu[i]!=w->gpu[i]){mismatch=i;error=-1;break;}}
            char line[512];
            uint32_t draw_started=a->uptime_ms();
            int begin=a->ioctl(display,DISPLAY_IOC_BEGIN_FRAME,NULL);
            if(begin<0){if(!error)error=begin;}
            else {
            frame=true;
            uint16_t black=COLOR_BLACK;
            int clear=a->ioctl(display,DISPLAY_IOC_CLEAR,&black);
            if(clear<0&&!error)error=clear;
            int title=text(a,display,8,COLOR_WHITE,"Fractal: C CPU  G FPGA  M Mandelbrot  J Julia  V compare  Q quit");
            if(title<0&&!error)error=title;
            if(!error)error=draw(a,display,&info,w,use_gpu?w->gpu:w->cpu,scale);
            draw_ms=a->uptime_ms()-draw_started;
            if(error){
                if(mismatch<PIXELS)a->snprintf(line,sizeof line,"COMPARE FAIL pixel %u,%u CPU %x FPGA %x",mismatch%32,mismatch/32,w->cpu[mismatch],w->gpu[mismatch]);
                else a->snprintf(line,sizeof line,"%s failed (%d). No CPU fallback. C selects CPU; Q quits.",use_gpu||compare?"FPGA/render":"CPU/render",error);
                text(a,display,32,0xf800,line);
            }else{
                char cpu_time[16],gpu_time[16],draw_time[16];
                milliseconds(a,cpu_time,cpu_ms,!use_gpu||compare);
                milliseconds(a,gpu_time,gpu_ms,use_gpu||compare);
                milliseconds(a,draw_time,draw_ms,true);
                a->snprintf(line,sizeof line,"%s / %s 32x32 x%u: CPU %s ms; FPGA call %s ms; draw %s ms%s",
                    scene?"Julia":"Mandelbrot",use_gpu?"FPGA":"CPU",scale,cpu_time,gpu_time,draw_time,compare?"; compare PASS1024":"");
                int status=text(a,display,32,COLOR_WHITE,line);if(status<0)error=status;
                if(use_gpu||compare){a->snprintf(line,sizeof line,"FPGA transport+compute %u us; engine cycles %u (%s)",w->request.elapsed_us,w->request.engine_cycles,w->request.cycles_valid?"valid":"unavailable");int timing=text(a,display,48,COLOR_WHITE,line);if(timing<0&&!error)error=timing;}
            }
            int end=a->ioctl(display,DISPLAY_IOC_END_FRAME,NULL);frame=false;
            if(end<0&&!error)error=end;
            draw_ms=a->uptime_ms()-draw_started;
            }
            int bytes=a->snprintf(line,sizeof line,
                "{\"fractal\":true,\"engine\":\"%s\",\"scene\":\"%s\",\"width\":32,\"height\":32,"
                "\"compare\":%s,\"mismatch\":%d,\"error\":%d,\"cpu_measured\":%s,\"cpu_ms\":%u,"
                "\"gpu_measured\":%s,\"gpu_ms\":%u,\"gpu_transport_compute_us\":%u,\"draw_ms\":%u}\n",
                use_gpu?"fpga":"cpu",scene?"julia":"mandelbrot",compare?"true":"false",
                mismatch<PIXELS?(int)mismatch:-1,error,!use_gpu||compare?"true":"false",cpu_ms,
                use_gpu||compare?"true":"false",gpu_ms,(use_gpu||compare)&&!error?w->request.elapsed_us:0,draw_ms);
            if(bytes>0){size_t count=(size_t)bytes;if(count>=sizeof line)count=sizeof line-1;a->write(console,line,count);}
            redraw=false;
        }
        uint8_t key=0;int n=a->read(console,&key,1);
        if(n<0)break;
        if(!n){a->sleep_ms(20);continue;}
        if(key=='q'||key=='Q'||key==27||key==3)break;
        if(key=='c'||key=='C'){use_gpu=false;compare=false;redraw=true;}
        if(key=='g'||key=='G'){use_gpu=true;compare=false;redraw=true;}
        if(key=='m'||key=='M'){scene=0;redraw=true;}
        if(key=='j'||key=='J'){scene=1;redraw=true;}
        if(key=='v'||key=='V'){compare=true;redraw=true;}
    }
done:
    if(frame)a->ioctl(display,DISPLAY_IOC_END_FRAME,NULL);
    if(paused){bool pause=false;a->ioctl(display,DISPLAY_IOC_CONSOLE_PAUSE,&pause);}
    if(w)a->release(w);
    if(fpga>=0)a->close(fpga);
    if(display>=0)a->close(display);
    if(focused){bool focus=false;a->ioctl(console,UART_IOC_FOCUS,&focus);}
    if(console>=0)a->close(console);
}
