#include "lab.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
static unsigned outstanding,allocations,fail_at;
extern "C" void *gfxbench_alloc(size_t n) {
    if(++allocations==fail_at)return nullptr;
    void *p=std::malloc(n);if(p)outstanding++;return p;
}
extern "C" void gfxbench_free(void *p) {if(p){assert(outstanding);outstanding--;std::free(p);}}
int main()
{
    using namespace lab;
    std::vector<uint16_t> c(width*height+2,0xdead),d(width*height+2,0xbeef);
    Target t{c.data()+1,d.data()+1};Work w;
    {
        Cpu cpu;CpuInteger integer;
        for(int raster=0;raster<2;raster++)for(int scene=0;scene<(raster?2:8);scene++)
            for(int load=0;load<3;load++)for(uint32_t frame:{0u,2u}) {
                std::fill(t.depth,t.depth+width*height,0);
                Request r{Scene(scene),load,frame};
                assert(raster?integer.render(r,t,w):cpu.render(r,t,w));
                auto color=checksum(t.color),depth=checksum(t.depth);
                assert(raster?integer.render(r,t,w):cpu.render(r,t,w));
                assert(checksum(t.color)==color&&checksum(t.depth)==depth);
                assert(c.front()==0xdead&&c.back()==0xdead&&d.front()==0xbeef&&d.back()==0xbeef);
                std::printf("%d,%d,%d,%u,%08x,%08x,%u,%u,%u,%u\n",raster,scene,load,frame,color,depth,w.vertices,w.lines,w.triangles,w.iterations);
            }
        assert(!cpu.render({Scene::Polygon,3,0},t,w));
        assert(!integer.render({Scene::Flow,0,0},t,w));
        Samples s;s.add(3,2);assert(s.size()==0);
        for(unsigned i=1;i<=120;i++)s.add(i,i+1);
        auto r=s.finish();assert(r.frames==120&&r.render_ms==60.5&&r.p95_ms==114&&r.pipeline_ms==61.5);
    }
    assert(!outstanding);
#ifdef GFXBENCH_NATIVE
    for(unsigned fault=1;fault<=3;fault++) {
        allocations=0;fail_at=fault;
        {Cpu cpu;assert(!cpu.render({Scene::Flow,0,0},t,w));}
        assert(!outstanding);
    }
#endif
}
