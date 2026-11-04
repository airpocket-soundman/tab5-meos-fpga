/* Standalone loaded M5EX app. Not embedded into the firmware. */
#include "../sdk/m5ex_sdk.h"
#include <m5os/dev_display.h>
#include <m5os/dev_fpga.h>
#include <m5os/dev_uart.h>
#include "../../third_party/carrier_link/fractal_reference.h"

M5EX_APP("fractal", M5_PERM_CONSOLE|M5_PERM_DISPLAY, 4096);
#define TILE_PIXELS 1024u
#define MAX_SIDE 256u
#define FRAME_PIXELS (MAX_SIDE*MAX_SIDE)
#define MAX_SCALE 16u

typedef struct {
    uint16_t cpu[FRAME_PIXELS],gpu[FRAME_PIXELS],tile[TILE_PIXELS];
    /* One expanded32-pixel source row, not a full512x512 framebuffer. */
    uint16_t strip[32u*MAX_SCALE*MAX_SCALE];
    fpga_fractal_request_t request;
} workspace_t;
static const fractal_config scenes[2]={
    {-10035,-4096,280,264,-2949,819,64,0},
    {-4096,-2048,132,132,-2949,819,64,1}
};
/* Time-based integer motion: engine speed never changes the animation phase. */
static int wave(uint32_t phase) {
    unsigned t=phase%8000u;
    return t<4000u?(int)t-2000:6000-(int)t;
}
static fractal_config animated_scene(unsigned scene,uint32_t phase) {
    fractal_config c=scenes[scene];
    if(scene){c.cx=(int16_t)(-2949+wave(phase)/4);c.cy=(int16_t)(819+wave(phase+2000u)/4);}
    else {
        unsigned step=128u+(unsigned)(wave(phase)+2000)/32u;
        c.dx=c.dy=(uint16_t)step;
        c.x0=(int16_t)(-3072-(int)(step*16u));
        c.y0=(int16_t)(-(int)(step*16u));
    }
    return c;
}
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
                workspace_t*w,const uint16_t*p,unsigned side,unsigned scale)
{
    unsigned width=side*scale;
    for(unsigned y=0;y<side;y++){
        for(unsigned sy=0;sy<scale;sy++)for(unsigned x=0;x<width;x++)
            w->strip[sy*width+x]=p[y*side+x/scale];
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
    r->iterations=c->limit;r->mode=c->julia;r->pixels=w->tile;r->pixel_count=TILE_PIXELS;
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
    /* The shell leaves the shared UART in blocking mode. The focus lease
     * restores that setting on exit. Use a bounded wait: a shared zero timeout
     * makes the higher-priority shell spin instead of yielding. */
    int32_t poll_timeout=10;
    if(a->ioctl(console,UART_IOC_SET_RX_TIMEOUT,&poll_timeout)<0)goto done;
    display=a->open("/dev/display",M5_O_WRITE);
    if(console<0||display<0)goto done;
    bool pause=true;
    if(a->ioctl(display,DISPLAY_IOC_CONSOLE_PAUSE,&pause)<0)goto done;
    paused=true;
    display_info_t info={0};
    if(a->ioctl(display,DISPLAY_IOC_GET_INFO,&info)<0||info.width<32||info.height<128)goto done;
    w=a->alloc(sizeof*w);if(!w)goto done;
    bool use_gpu=false,compare=false,redraw=true,animate=true;unsigned scene=1,side=128;
    uint8_t pending_key=0;
    uint32_t clock=a->uptime_ms(),phase_ms=0,last_start=clock,last_end=clock,fps_milli=0,frames=0;
    for(;;){
        uint32_t now=a->uptime_ms(),delta=now-clock;clock=now;
        if(animate)phase_ms+=delta;
        if(animate&&(uint32_t)(now-last_start)>=100u)redraw=true;
        if(redraw){
            last_start=now;
            fractal_config current=animated_scene(scene,phase_ms);
            current.dx=(uint16_t)((unsigned)current.dx*32u/side);
            current.dy=(uint16_t)((unsigned)current.dy*32u/side);
            unsigned total=side*side,tiles=0,compared_pixels=0;
            unsigned scale=512u/side,vertical=(info.height-96u)/side;
            if(scale>info.width/side)scale=info.width/side;
            if(scale>vertical)scale=vertical;
            uint32_t cpu_ms=0,gpu_ms=0,draw_ms=0,work=0,device_us=0,device_cycles=0;
            bool cycles_valid=true;
            int error=scale?0:-1;unsigned mismatch=total;bool cancelled=false;
            if((use_gpu||compare)&&fpga<0)fpga=a->open("/dev/fpga",M5_O_READ|M5_O_WRITE);
            for(unsigned ty=0;ty<side&&!error&&!cancelled;ty+=32)for(unsigned tx=0;tx<side&&!error&&!cancelled;tx+=32){
                fractal_config tile=current;
                int32_t x0=(int32_t)current.x0+(int32_t)tx*current.dx;
                int32_t y0=(int32_t)current.y0+(int32_t)ty*current.dy;
                if(x0<INT16_MIN||x0>INT16_MAX||y0<INT16_MIN||y0>INT16_MAX){error=-1;break;}
                tile.x0=(int16_t)x0;tile.y0=(int16_t)y0;
                if(!fractal_config_valid(&tile)){error=-1;break;}
                if(!use_gpu||compare){uint32_t t=a->uptime_ms();
                    if(!fractal_reference_tile(&tile,w->tile,&work))error=-1;
                    cpu_ms+=a->uptime_ms()-t;
                    if(!error)for(unsigned y=0;y<32;y++)memcpy(w->cpu+(ty+y)*side+tx,w->tile+y*32,64);
                }
                if(!error&&(use_gpu||compare)){
                    uint32_t t=a->uptime_ms();error=fpga<0?fpga:gpu(a,fpga,w,&tile);gpu_ms+=a->uptime_ms()-t;
                    if(!error){device_us+=w->request.elapsed_us;
                        if(!w->request.cycles_valid||device_cycles>UINT32_MAX-w->request.engine_cycles)cycles_valid=false;
                        else device_cycles+=w->request.engine_cycles;
                        for(unsigned y=0;y<32;y++)memcpy(w->gpu+(ty+y)*side+tx,w->tile+y*32,64);
                    }
                    if(!error){uint8_t key=0;int n=a->read(console,&key,1);
                        if(n<0)error=n;
                        else if(n>0){pending_key=key;cancelled=true;}
                    }
                }
                if(!error&&!cancelled)tiles++;
                if(!error&&!cancelled&&!use_gpu&&!compare&&tx+32==side){
                    uint8_t key=0;int n=a->read(console,&key,1);
                    if(n<0)error=n;
                    else if(n>0){pending_key=key;cancelled=true;}
                }
            }
            if(!error&&!cancelled&&compare){
                for(unsigned i=0;i<total;i++){compared_pixels++;if(w->cpu[i]!=w->gpu[i]){mismatch=i;error=-1;break;}}
            }
            if(cancelled){redraw=false;goto input;}
            char line[512];
            uint32_t draw_started=a->uptime_ms();
            int begin=a->ioctl(display,DISPLAY_IOC_BEGIN_FRAME,NULL);
            if(begin<0){if(!error)error=begin;}
            else {
            frame=true;
            uint16_t black=COLOR_BLACK;
            int clear=a->ioctl(display,DISPLAY_IOC_CLEAR,&black);
            if(clear<0&&!error)error=clear;
            int title=text(a,display,8,COLOR_WHITE,"Fractal: C CPU G FPGA M/J scene V compare R resolution SPACE/A pause Q quit");
            if(title<0&&!error)error=title;
            a->snprintf(line,sizeof line,"%s  %u.%u FPS  SPACE pause/resume",animate?"PLAY":"PAUSE",fps_milli/1000u,(fps_milli%1000u)/100u);
            int motion=text(a,display,24,COLOR_WHITE,line);if(motion<0&&!error)error=motion;
            if(!error)error=draw(a,display,&info,w,use_gpu?w->gpu:w->cpu,side,scale);
            draw_ms=a->uptime_ms()-draw_started;
            if(error){
                if(mismatch<total)a->snprintf(line,sizeof line,"COMPARE FAIL pixel %u,%u CPU %x FPGA %x",mismatch%side,mismatch/side,w->cpu[mismatch],w->gpu[mismatch]);
                else a->snprintf(line,sizeof line,"%s failed (%d). No CPU fallback. C selects CPU; Q quits.",use_gpu||compare?"FPGA/render":"CPU/render",error);
                text(a,display,32,0xf800,line);
            }else{
                char cpu_time[16],gpu_time[16],draw_time[16];
                milliseconds(a,cpu_time,cpu_ms,!use_gpu||compare);
                milliseconds(a,gpu_time,gpu_ms,use_gpu||compare);
                milliseconds(a,draw_time,draw_ms,true);
                a->snprintf(line,sizeof line,"%s / %s %ux%u x%u: CPU %s ms; FPGA %s ms; draw %s ms; compare %u",
                    scene?"Julia":"Mandelbrot",use_gpu?"FPGA":"CPU",side,side,scale,cpu_time,gpu_time,draw_time,compared_pixels);
                int status=text(a,display,32,COLOR_WHITE,line);if(status<0)error=status;
                if(use_gpu||compare){a->snprintf(line,sizeof line,"FPGA transport+compute %u us; %u tiles; engine cycles %u (%s)",device_us,tiles,device_cycles,cycles_valid?"valid":"unavailable");int timing=text(a,display,48,COLOR_WHITE,line);if(timing<0&&!error)error=timing;}
            }
            int end=a->ioctl(display,DISPLAY_IOC_END_FRAME,NULL);frame=false;
            if(end<0&&!error)error=end;
            draw_ms=a->uptime_ms()-draw_started;
            }
            uint32_t ended=a->uptime_ms(),interval=ended-last_end;last_end=ended;
            fps_milli=interval?1000000u/interval:0;frames++;
            if(error)animate=false;
            int bytes=a->snprintf(line,sizeof line,
                "{\"fractal\":true,\"frame\":%u,\"animation\":%s,\"phase_ms\":%u,\"fps_milli\":%u,\"engine\":\"%s\",\"scene\":\"%s\",\"width\":%u,\"height\":%u,\"tiles\":%u,\"compared_pixels\":%u,"
                "\"compare\":%s,\"mismatch\":%d,\"error\":%d,\"cpu_measured\":%s,\"cpu_ms\":%u,"
                "\"gpu_measured\":%s,\"gpu_ms\":%u,\"gpu_transport_compute_us\":%u,\"draw_ms\":%u}\n",
                frames,animate?"true":"false",phase_ms,fps_milli,use_gpu?"fpga":"cpu",scene?"julia":"mandelbrot",side,side,tiles,compared_pixels,compare?"true":"false",
                mismatch<total?(int)mismatch:-1,error,!use_gpu||compare?"true":"false",cpu_ms,
                use_gpu||compare?"true":"false",gpu_ms,(use_gpu||compare)&&!error?device_us:0,draw_ms);
            if(bytes>0){size_t count=(size_t)bytes;if(count>=sizeof line)count=sizeof line-1;a->write(console,line,count);}
            redraw=false;
        }
input:;
        uint8_t key=pending_key;int n=key?1:a->read(console,&key,1);pending_key=0;
        if(n<0)break;
        if(!n){a->sleep_ms(20);continue;}
        if(key=='q'||key=='Q'||key==27||key==3)break;
        if(key=='c'||key=='C'){use_gpu=false;compare=false;redraw=true;}
        if(key=='g'||key=='G'){use_gpu=true;compare=false;redraw=true;}
        if(key=='m'||key=='M'){scene=0;redraw=true;}
        if(key=='j'||key=='J'){scene=1;redraw=true;}
        if(key=='r'||key=='R'){side=side==256?32:side*2;redraw=true;}
        if(key=='v'||key=='V'){compare=true;redraw=true;}
        if(key==' '||key=='a'||key=='A'){animate=!animate;clock=a->uptime_ms();redraw=true;}
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
