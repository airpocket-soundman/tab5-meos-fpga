#include "gfxbench_app.h"
#include "../../third_party/gpu_lab/main/lab.hpp"
#include <cstring>
#include <new>
extern "C" {
#include <m5os/display.h>
#include <m5os/hal_time.h>
#include <m5os/kmalloc.h>
#include <m5os/kprintf.h>
#include <m5os/mutex.h>
#include <m5os/task.h>
#include "../../drivers/lcd/fbcon.h"
#include "../tui/dqwin.h"
}

extern "C" void *gfxbench_alloc(size_t n) {return kmalloc_psram(n);}
extern "C" void gfxbench_free(void *p) {kfree(p);}
namespace {
constexpr const char *scenes[]={"wire","polygon","mandelbrot","julia","particles","galaxy","fire","flow"};
struct Options {
    lab::Request request{lab::Scene::Polygon,0,0};
    bool integer=false,bench=false,suite=false,headless=false;
    unsigned frames=0;
};
struct State {
    lab::Cpu cpu;
    lab::CpuInteger integer;
    lab::Samples samples;
    lab::Work work;
    uint16_t *color=nullptr,*depth=nullptr;
};
static mutex_t screen_lock=MUTEX_INIT;
bool parse_uint(const char *s,unsigned &out)
{
    if(!*s)return false;
    unsigned n=0;
    for(;*s;s++) {if(*s<'0'||*s>'9'||n>(10000u-unsigned(*s-'0'))/10)return false;n=n*10+unsigned(*s-'0');}
    out=n;return n>0;
}
int usage()
{
    kprintf("gfxbench [wire|polygon|mandelbrot|julia|particles|galaxy|fire|flow|suite|list]\n"
            "         [light|medium|heavy] [float|integer] [--bench | --frames 1..10000] [--headless]\n"
            "Keys: N scene, L load, R raster, B benchmark, Space pause, Q/Esc quit. CPU only.\n");
    return 1;
}
bool parse(int argc,char **argv,Options &o)
{
    bool scene=false,load=false,raster=false;
    for(int i=1;i<argc;i++) {
        const char *a=argv[i];bool found=false;
        for(unsigned s=0;s<8;s++)if(!std::strcmp(a,scenes[s])) {
            if(scene)return false;
            scene=true;o.request.scene=lab::Scene(s);found=true;break;
        }
        if(found)continue;
        if(!std::strcmp(a,"suite")) {if(scene)return false;scene=true;o.suite=true;o.bench=true;}
        else if(!std::strcmp(a,"light")||!std::strcmp(a,"medium")||!std::strcmp(a,"heavy")) {
            if(load)return false;
            load=true;o.request.load=!std::strcmp(a,"light")?0:!std::strcmp(a,"medium")?1:2;
        } else if(!std::strcmp(a,"float")||!std::strcmp(a,"integer")) {
            if(raster)return false;
            raster=true;o.integer=!std::strcmp(a,"integer");
        } else if(!std::strcmp(a,"--bench"))o.bench=true;
        else if(!std::strcmp(a,"--headless"))o.headless=true;
        else if(!std::strcmp(a,"--frames")) {
            if(o.frames||++i>=argc||!parse_uint(argv[i],o.frames))return false;
        } else return false;
    }
    if(o.suite) {if(load)return false;o.request={lab::Scene::Wire,0,0};}
    if(o.integer&&int(o.request.scene)>1)return false;
    return !(o.frames&&o.bench)&&!(o.headless&&!o.frames&&!o.bench);
}
void hud(const Options &o,const State &s,bool paused,uint64_t elapsed)
{
    char text[160];
    ksnprintf(text,sizeof text,"GPU LAB / CPU / %s / %s / %s",scenes[int(o.request.scene)],lab::load_name(o.request.load),o.integer?"integer_g0":"legacy_float");
    display_fill_rect(0,0,DISPLAY_WIDTH,60,COLOR_BLACK);
    display_draw_text(8,8,text,COLOR_CYAN,COLOR_BLACK,2);
    ksnprintf(text,sizeof text,"frame %u  render %u.%03u ms  %s  samples %u/120",
              o.request.frame,unsigned(elapsed/1000),unsigned(elapsed%1000),paused?"PAUSED":o.bench?"BENCH":"LIVE",unsigned(s.samples.size()));
    display_draw_text(8,34,text,COLOR_WHITE,COLOR_BLACK,1);
}
bool present(const Options &o,const State &s,bool paused,uint64_t elapsed)
{
    bool batch=display_begin_frame();
    hud(o,s,paused,elapsed);
    int scale=(DISPLAY_WIDTH>=1280&&DISPLAY_HEIGHT>=784)?2:1;
    display_draw_bitmap_scaled((DISPLAY_WIDTH-lab::width*scale)/2,
        64+(DISPLAY_HEIGHT-96-lab::height*scale)/2,lab::width,lab::height,s.color,scale);
    display_draw_text(8,DISPLAY_HEIGHT-24,"N:Scene  L:Load  R:Raster  B:Bench  Space:Pause  Q:Quit  | CPU only",COLOR_GRAY,COLOR_BLACK,1);
    return !batch||display_end_frame();
}
void result(const Options &o,const State &s)
{
    auto r=s.samples.finish();
    if(!r.valid||r.frames!=lab::sample_frames)return;
    kprintf("GFXBENCH,1,CPU,%s,%s,%u,640,360,%u,%u,%u,%u,%u\n",
            o.integer?"integer_g0":"legacy_float",scenes[int(o.request.scene)],unsigned(o.request.load),r.frames,
            unsigned(r.render_ms*1000),unsigned(r.p95_ms*1000),unsigned(r.pipeline_ms*1000),unsigned(r.fps*1000));
}
bool next_case(Options &o)
{
    o.request.frame=0;
    if(++o.request.load<3)return true;
    o.request.load=0;
    int scene=int(o.request.scene)+1;
    if(scene>=(o.integer?2:8))return false;
    o.request.scene=lab::Scene(scene);return true;
}
} // namespace

extern "C" int gfxbench_cmd_main(int argc,char **argv)
{
    if(argc<1||!argv)return usage();
    if(argc==2&&!std::strcmp(argv[1],"list")) {
        for(unsigned i=0;i<8;i++)kprintf("%s: %s%s\n",scenes[i],lab::name(lab::Scene(i)),i<2?" (float / integer)":" (float)");
        kprintf("CPU only. FPGA polygons are unavailable; Dock GFX v0 supports 32x32 points/rectangles.\n");return 0;
    }
    Options o;if(!parse(argc,argv,o))return usage();
    bool exit_after_bench=o.bench;
    if(!mutex_trylock(&screen_lock)){kprintf("gfxbench: busy\n");return 1;}
    void *storage=kmalloc(sizeof(State));
    State *s=storage?new(storage) State:nullptr;
    if(s) {s->color=static_cast<uint16_t *>(gfxbench_alloc(lab::width*lab::height*2));
           s->depth=static_cast<uint16_t *>(gfxbench_alloc(lab::width*lab::height*2));}
    if(!s||!s->color||!s->depth) {
        if(s){kfree(s->color);kfree(s->depth);s->~State();kfree(s);}
        mutex_unlock(&screen_lock);kprintf("gfxbench: insufficient PSRAM\n");return 1;
    }
    bool was_paused=fbcon_is_paused(),paused=false;
    unsigned rendered=0,cases=0;int status=0;uint64_t render_us=0;
    if(!o.headless) {fbcon_pause(true);task_sleep(20);display_clear(COLOR_BLACK);}
    if(o.bench)kprintf("GFXBENCH_HEADER,schema,backend,raster,scene,load,width,height,frames,render_mean_us,render_p95_us,pipeline_mean_us,frame_fps_milli\n");
    for(;;) {
        int key=dq_pollkey();
        if(key=='q'||key=='Q'||key==DQK_ESC||key==3) {if(o.bench||key==3)status=130;break;}
        if(key==' ')paused=!paused;
        bool changed=false;
        if(key=='n'||key=='N'){o.request.scene=lab::Scene((int(o.request.scene)+1)%(o.integer?2:8));changed=true;}
        if(key=='l'||key=='L'){o.request.load=(o.request.load+1)%3;changed=true;}
        if((key=='r'||key=='R')&&int(o.request.scene)<2){o.integer=!o.integer;changed=true;}
        if(changed) {o.request.frame=0;o.bench=o.suite=false;s->samples.clear();}
        if(key=='b'||key=='B') {
            kprintf("GFXBENCH_HEADER,schema,backend,raster,scene,load,width,height,frames,render_mean_us,render_p95_us,pipeline_mean_us,frame_fps_milli\n");
            o.request.frame=0;o.bench=true;o.suite=false;paused=false;s->samples.clear();
        }
        if(paused) {if(!o.headless){bool batch=display_begin_frame();hud(o,*s,true,render_us);if(batch&&!display_end_frame()){status=1;break;}}task_sleep(10);continue;}
        uint64_t start=hal_time_now_us();
        bool ok=o.integer?s->integer.render(o.request,{s->color,s->depth},s->work):s->cpu.render(o.request,{s->color,s->depth},s->work);
        render_us=hal_time_now_us()-start;
        if(!ok) {kprintf("gfxbench: render failed (allocation or unsupported request)\n");status=1;break;}
        if(!o.headless&&!present(o,*s,false,render_us)){kprintf("gfxbench: LCD presentation failed\n");status=1;break;}
        task_sleep(1);
        uint64_t pipeline_us=hal_time_now_us()-start;
        rendered++;
        if(o.bench&&o.request.frame>=lab::warmup_frames)s->samples.add(double(render_us)/1000,double(pipeline_us)/1000);
        o.request.frame++;
        if(o.bench&&s->samples.size()==lab::sample_frames) {
            result(o,*s);cases++;
            if(o.suite) {if(!next_case(o))break;s->samples.clear();}
            else if(exit_after_bench)break;
            else {o.bench=false;o.request.frame=0;}
        }
        if(o.frames&&rendered>=o.frames)break;
    }
    unsigned hash=rendered?lab::checksum(s->color):0;
    if(!o.headless)fbcon_pause(was_paused);
    kprintf("{\"program\":\"gfxbench\",\"backend\":\"CPU\",\"status\":%d,\"frames_rendered\":%u,\"benchmark_cases\":%u,\"checksum\":%u}\n",status,rendered,cases,hash);
    kfree(s->color);kfree(s->depth);s->~State();kfree(s);
    mutex_unlock(&screen_lock);return status;
}
