// Adapted for native MeOS; provenance and changes: third_party/gpu_lab/README.md
#include "lab.hpp"
#include "../reference/cpp/integer_raster.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include "native_buffer.hpp"
#include <new>

namespace lab {
namespace {
uint16_t rgb(int r, int g, int b) { return uint16_t(((r & 248) << 8) | ((g & 252) << 3) | (b >> 3)); }
uint16_t palette(int n) { return rgb(32 + (n * 9 % 224), 60 + (n * 5 % 196), 100 + (n * 3 % 156)); }
void pixel(Target t, int x, int y, uint16_t c) {
    if (unsigned(x) < width && unsigned(y) < height) t.color[y * width + x] = c;
}
void line(Target t, int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        pixel(t, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e = 2 * err;
        if (e >= dy) { err += dy; x0 += sx; }
        if (e <= dx) { err += dx; y0 += sy; }
    }
}
struct V { float x, y, z; };
float edge(V a, V b, float x, float y) { return (x-a.x)*(b.y-a.y)-(y-a.y)*(b.x-a.x); }
void triangle(Target t, V a, V b, V c, uint16_t color) {
    float area = edge(a,b,c.x,c.y);
    if (std::abs(area) < 0.01f) return;
    if (area < 0) { std::swap(b,c); area = -area; }
    int x0 = std::max(0, int(std::floor(std::min({a.x,b.x,c.x}))));
    int x1 = std::min(width-1, int(std::ceil(std::max({a.x,b.x,c.x}))));
    int y0 = std::max(0, int(std::floor(std::min({a.y,b.y,c.y}))));
    int y1 = std::min(height-1, int(std::ceil(std::max({a.y,b.y,c.y}))));
    const float inverse = 1.f / area;
    for (int y=y0; y<=y1; ++y) {
        float wa = edge(b,c,x0+0.5f,y+0.5f), wb = edge(c,a,x0+0.5f,y+0.5f);
        float wc = edge(a,b,x0+0.5f,y+0.5f);
        for (int x=x0; x<=x1; ++x) {
            if (wa>=0 && wb>=0 && wc>=0) {
                // Interpolate reciprocal camera depth for perspective-correct visibility.
                float iz = (wa*a.z + wb*b.z + wc*c.z)*inverse;
                uint16_t z = uint16_t(std::min(65535.f, iz*50000.f));
                int p = y*width+x;
                if (z > t.depth[p]) { t.depth[p]=z; t.color[p]=color; }
            }
            wa += c.y-b.y; wb += a.y-c.y; wc += b.y-a.y;
        }
    }
}
void cubes(const Request &r, Target t, Work &w) {
    static constexpr int edges[12][2]={{0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{2,6},{3,7}};
    static constexpr int faces[12][3]={{0,1,3},{0,3,2},{4,6,7},{4,7,5},{0,4,5},{0,5,1},{2,3,7},{2,7,6},{0,2,6},{0,6,4},{1,5,7},{1,7,3}};
    int count = std::array<int,3>{16,64,144}[r.load];
    int cols = int(std::sqrt(float(count))), rows=(count+cols-1)/cols;
    for (int n=0; n<count; ++n) {
        float angle = r.frame * 0.025f + n*0.17f;
        float s=std::sin(angle), c=std::cos(angle), s2=std::sin(angle*0.7f), c2=std::cos(angle*0.7f);
        float cell=std::min(float(width)/cols,float(height)/rows), scale=cell*0.92f;
        V p[8];
        for (int i=0;i<8;++i) {
            float x=(i&1)?1.f:-1.f, y=(i&2)?1.f:-1.f, z=(i&4)?1.f:-1.f;
            float xx=x*c+z*s, zz=-x*s+z*c, yy=y*c2-zz*s2, depth=4.f+y*s2+zz*c2;
            p[i]={(n%cols+0.5f)*width/cols+xx*scale/depth,
                  (n/cols+0.5f)*height/rows+yy*scale/depth,1.f/depth};
        }
        w.vertices+=8;
        if (r.scene==Scene::Wire) {
            for (auto &e:edges) line(t,int(p[e[0]].x),int(p[e[0]].y),int(p[e[1]].x),int(p[e[1]].y),palette(n+12));
            w.lines+=12;
        } else {
            for (int f=0;f<12;++f) triangle(t,p[faces[f][0]],p[faces[f][1]],p[faces[f][2]],palette(f*7+n));
            w.triangles+=12;
        }
    }
}
void fractal(const Request &r, Target t, Work &w) {
    int limit=std::array<int,3>{24,64,128}[r.load];
    float phase=r.frame*0.013f;
    float zoom=1.f+0.45f*std::sin(phase*0.3f);
    float jx=-0.72f+0.08f*std::cos(phase), jy=0.2f+0.08f*std::sin(phase);
    for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
        float px=(x-width*0.5f)*(3.4f/width)/zoom, py=(y-height*0.5f)*(3.4f/width)/zoom;
        bool julia=r.scene==Scene::Julia;
        float zx=julia?px:0, zy=julia?py:0, cx=julia?jx:px-0.6f, cy=julia?jy:py;
        int i=0;
        while (zx*zx+zy*zy<4 && i<limit) { float next=zx*zx-zy*zy+cx; zy=2*zx*zy+cy; zx=next; ++i; }
        w.iterations+=i;
        t.color[y*width+x]=i==limit?rgb(3,8,20):palette(i*3);
    }
}
void particles(const Request &r, Target t, Work &w) {
    int count=std::array<int,3>{512,2048,8192}[r.load];
    uint32_t seed=0x12345678;
    for (int n=0;n<count;++n) {
        seed=seed*1664525u+1013904223u;
        float a=float(seed&65535)*0.000095874f+r.frame*0.012f;
        float radius=20.f+float(seed>>16)*0.0021f;
        int x=int(width*0.5f+std::cos(a)*radius*1.7f), y=int(height*0.5f+std::sin(a)*radius);
        uint16_t color=palette(n);
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) pixel(t,x+dx,y+dy,color);
    }
    w.vertices=count;
}
uint32_t random32(uint32_t &s) { s=s*1664525u+1013904223u; return s; }
void glow(Target t,int x,int y,int r,int g,int b) {
    if(unsigned(x)>=width || unsigned(y)>=height) return;
    auto &p=t.color[y*width+x];
    p=rgb(std::min(255,int((p>>11)*255/31)+r),std::min(255,int(((p>>5)&63)*255/63)+g),std::min(255,int((p&31)*255/31)+b));
}
void galaxy(const Request &r,Target t,Work &w) {
    int count=std::array<int,3>{1024,4096,16384}[r.load];
    uint32_t seed=0x67616c61;
    for(int n=0;n<count;++n) {
        float u=float(random32(seed)&65535)/65535.f;
        float radius=8+std::sqrt(u)*260;
        float jitter=(float(random32(seed)&65535)/65535.f-0.5f)*0.75f;
        // Circular Kepler orbits around one fixed central point mass.
        float omega=45.f/std::sqrt(radius*radius*radius);
        float angle=radius*0.031f+(n%2)*3.14159265f+jitter+r.frame*omega;
        int x=int(width*0.5f+radius*std::cos(angle));
        int y=int(height*0.5f+radius*std::sin(angle)*0.52f);
        bool blue=(seed&7)>2;
        glow(t,x,y,blue?50:180,blue?100:110,blue?200:50);
        if((seed&31)==0) { glow(t,x+1,y,40,70,120); glow(t,x,y+1,40,70,120); }
    }
    for(int y=-8;y<=8;++y) for(int x=-12;x<=12;++x) {
        float a=std::exp(-(x*x+y*y)/35.f);
        glow(t,width/2+x,height/2+y,int(255*a),int(220*a),int(150*a));
    }
    w.vertices=count;
}
float noise(int x,int y,int frame) {
    uint32_t v=uint32_t(x)*374761393u+uint32_t(y)*668265263u+uint32_t(frame)*2246822519u;
    v=(v^(v>>13))*1274126177u; return float((v^(v>>16))&65535)/65535.f;
}
float smooth_noise(float x,float y,int frame) {
    int ix=int(std::floor(x)),iy=int(std::floor(y)); float fx=x-ix,fy=y-iy;
    fx=fx*fx*(3-2*fx); fy=fy*fy*(3-2*fy);
    float a=noise(ix,iy,frame)*(1-fx)+noise(ix+1,iy,frame)*fx;
    float b=noise(ix,iy+1,frame)*(1-fx)+noise(ix+1,iy+1,frame)*fx;
    return a*(1-fy)+b*fy;
}
void fire(const Request &r,Target t,Work &w) {
    int octaves=std::array<int,3>{2,4,6}[r.load];
    float time=r.frame*0.045f;
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        float nx=(x-width*0.5f)/(width*0.5f),h=float(height-1-y)/height;
        float turbulence=0,amp=0.5f,freq=3;
        for(int o=0;o<octaves;++o) {
            turbulence+=amp*smooth_noise(nx*freq+12,h*freq-time*(1+o*0.2f),17+o);
            freq*=2; amp*=0.5f;
        }
        float heat=std::clamp((1-h)*1.5f-std::abs(nx)*(0.5f+h)+turbulence*0.85f-0.7f,0.f,1.f);
        t.color[y*width+x]=rgb(int(std::min(1.f,heat*3)*255),int(std::clamp(heat*3-0.8f,0.f,1.f)*240),int(std::clamp(heat*4-2.5f,0.f,1.f)*160));
    }
    w.iterations=width*height*octaves;
}
} // namespace
struct Cpu::FlowState {
    struct Particle { float x,y; };
    NativeBuffer<Particle,8192> particles;
    NativeBuffer<uint16_t,width*height> trail;
    int load=-1;
    uint32_t next_frame=0,seed=0;
    std::array<Particle,4> vortex;
    std::array<float,4> spin;
    static constexpr float cx=width*0.42f,cy=height*0.5f,radius=34;
    void reset(int selected) {
        load=selected; next_frame=0; seed=0x666c6f77;
        particles.resize(std::array<int,3>{512,2048,8192}[load]); trail.assign(width*height,0);
        for(auto &p:particles) { p.x=float(random32(seed)%width); p.y=float(random32(seed)%height); }
    }
    Particle velocity(float x,float y) const {
        float dx=x-cx,dy=y-cy,rr=std::max(dx*dx+dy*dy,radius*radius);
        float factor=radius*radius/(rr*rr);
        Particle v{3.2f*(1-factor*(dx*dx-dy*dy)),-6.4f*factor*dx*dy};
        // Alternating soft vortices shed downstream of the obstacle.
        if(x>cx) for(int k=0;k<4;++k) {
            float rx=x-vortex[k].x,ry=y-vortex[k].y;
            float weight=spin[k]/(rx*rx+ry*ry+350);
            v.x-=ry*weight; v.y+=rx*weight;
        }
        return v;
    }
    void step(uint32_t frame) {
        float phase=frame*0.025f;
        for(int k=0;k<4;++k) {
            vortex[k]={cx+70+k*76,cy+(k%2?1.f:-1.f)*(26+10*std::sin(phase+k))};
            spin[k]=(k%2?1.f:-1.f)*(100+30*std::sin(phase*0.8f+k));
        }
        for(auto &p:trail) {
            int r=(p>>11)*29/32,g=((p>>5)&63)*29/32,b=(p&31)*29/32;
            p=uint16_t((r<<11)|(g<<5)|b);
        }
        Target target{trail.data(),nullptr};
        for(size_t n=0;n<particles.size();++n) {
            auto &p=particles[n];
            for(int sub=0;sub<2;++sub) {
                auto a=velocity(p.x,p.y);
                auto b=velocity(p.x+a.x*0.25f,p.y+a.y*0.25f);
                float nx=p.x+b.x*0.5f,ny=p.y+b.y*0.5f;
                float dx=nx-cx,dy=ny-cy,dist=std::sqrt(dx*dx+dy*dy);
                if(dist<radius+1) { nx=cx+dx*(radius+1)/std::max(dist,0.01f); ny=cy+dy*(radius+1)/std::max(dist,0.01f); }
                if(nx>=width || nx<0 || ny<0 || ny>=height) {
                    p={float(random32(seed)%12),float(random32(seed)%height)}; break;
                }
                line(target,int(p.x),int(p.y),int(nx),int(ny),rgb(35,130+int(n%3)*35,230));
                p={nx,ny};
            }
            pixel(target,int(p.x),int(p.y),rgb(140,235,255));
        }
    }
    void render(const Request &r,Target t,Work &w) {
        if(load!=r.load || r.frame!=next_frame) reset(r.load);
        while(next_frame<=r.frame) step(next_frame++);
        std::copy(trail.begin(),trail.end(),t.color);
        for(int y=int(cy-radius-1);y<=int(cy+radius+1);++y) for(int x=int(cx-radius-1);x<=int(cx+radius+1);++x) {
            float d=(x-cx)*(x-cx)+(y-cy)*(y-cy);
            if(d<radius*radius) pixel(t,x,y,d>(radius-2)*(radius-2)?rgb(255,175,100):rgb(22,31,48));
        }
        w.vertices=particles.size(); w.lines=particles.size()*2; w.iterations=particles.size()*2;
    }
};
Cpu::Cpu()=default;
Cpu::~Cpu() { if(flow_) {flow_->~FlowState();gfxbench_free(flow_);} }
const char *name(Scene s) {
    static const char *names[]={"Wireframe fleet","Solid / Z-buffer","Mandelbrot zoom","Julia morph","Particle vortex","Kepler galaxy","Procedural fire","Flow / cylinder wake"};
    return names[int(s)];
}
const char *load_name(int load) { return std::array<const char *,3>{"LIGHT","MEDIUM","HEAVY"}[load]; }
bool Cpu::render(const Request &r, Target t, Work &w) {
    if (!t.color || !t.depth || r.load<0 || r.load>=load_count || int(r.scene)<0 || int(r.scene)>=scene_count) return false;
    w={};
    std::fill(t.color,t.color+width*height,rgb(5,10,23));
    if (r.scene==Scene::Polygon) std::fill(t.depth,t.depth+width*height,0);
    switch(r.scene) {
        case Scene::Wire: case Scene::Polygon: cubes(r,t,w); break;
        case Scene::Mandelbrot: case Scene::Julia: fractal(r,t,w); break;
        case Scene::Particles: particles(r,t,w); break;
        case Scene::Galaxy: galaxy(r,t,w); break;
        case Scene::Fire: fire(r,t,w); break;
        case Scene::Flow:
            if(!flow_) {
                void *storage=gfxbench_alloc(sizeof(FlowState));
                if(!storage) return false;
                flow_=new(storage) FlowState;
                if(!flow_->particles.valid() || !flow_->trail.valid()) {
                    flow_->~FlowState();gfxbench_free(flow_);flow_=nullptr;return false;
                }
            }
            flow_->render(r,t,w); break;
    }
    return true;
}
bool CpuInteger::render(const Request &r, Target t, Work &w) {
    if (!t.color || !t.depth || r.load<0 || r.load>=load_count || !available(r.scene)) return false;
    w={};
    integer_raster::FrameView target(width,height,t.color,t.depth);
    std::fill(t.color,t.color+width*height,rgb(5,10,23));
    if (r.scene==Scene::Polygon) std::fill(t.depth,t.depth+width*height,0);
    static constexpr int edges[12][2]={{0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{2,6},{3,7}};
    static constexpr int faces[12][3]={{0,1,3},{0,3,2},{4,6,7},{4,7,5},{0,4,5},{0,5,1},{2,3,7},{2,7,6},{0,2,6},{0,6,4},{1,5,7},{1,7,3}};
    const int count=std::array<int,3>{16,64,144}[r.load];
    const int cols=int(std::sqrt(float(count))), rows=(count+cols-1)/cols;
    for (int n=0; n<count; ++n) {
        const float angle=r.frame*0.025f+n*0.17f;
        const float s=std::sin(angle), c=std::cos(angle), s2=std::sin(angle*0.7f), c2=std::cos(angle*0.7f);
        const float cell=std::min(float(width)/cols,float(height)/rows), scale=cell*0.92f;
        integer_raster::Vertex p[8];
        for (int i=0; i<8; ++i) {
            const float x=(i&1)?1.f:-1.f, y=(i&2)?1.f:-1.f, z=(i&4)?1.f:-1.f;
            const float xx=x*c+z*s, zz=-x*s+z*c, yy=y*c2-zz*s2;
            const float camera_depth=4.f+y*s2+zz*c2;
            // This fixed cube scene stays beyond depth 4-sqrt(3)>2.26.
            // It does not expose a general mesh/near-plane clipping API.
            if (!std::isfinite(camera_depth) || camera_depth<=0.f) return false;
            const float px=(n%cols+0.5f)*width/cols+xx*scale/camera_depth;
            const float py=(n/cols+0.5f)*height/rows+yy*scale/camera_depth;
            const auto qx=std::int64_t(std::floor(px*16.f)), qy=std::int64_t(std::floor(py*16.f));
            const auto qz=std::int64_t(std::floor(50000.f/camera_depth));
            if (qx < -32768 || qx > 32767 || qy < -32768 || qy > 32767 || qz<0 || qz>65535) return false;
            p[i]={qx,qy,qz};
        }
        w.vertices+=8;
        if (r.scene==Scene::Wire) {
            for (const auto &e:edges) target.line(p[e[0]],p[e[1]],palette(n+12),std::nullopt,false);
            w.lines+=12;
        } else {
            for (int f=0; f<12; ++f) target.triangle(p[faces[f][0]],p[faces[f][1]],p[faces[f][2]],palette(f*7+n),std::nullopt,false);
            w.triangles+=12;
        }
    }
    return true;
}
void Samples::clear() { count_=0; pipeline_sum_=0; }
void Samples::add(double render, double pipeline) {
    if(count_>=render_.size() || !std::isfinite(render) || !std::isfinite(pipeline) || render<0 || pipeline<=0 || pipeline<render) return;
    render_[count_++]=render; pipeline_sum_+=pipeline;
}
Result Samples::finish() const {
    if (!count_) return {};
    auto sorted=render_;
    std::sort(sorted.begin(),sorted.begin()+count_);
    double sum=std::accumulate(sorted.begin(),sorted.begin()+count_,0.0);
    return {true,sum/count_,sorted[size_t(std::ceil(count_*0.95))-1],pipeline_sum_/count_,1000.0*count_/pipeline_sum_,uint32_t(count_)};
}
uint32_t checksum(const uint16_t *p) {
    uint32_t hash=2166136261u;
    for(int i=0;i<width*height;++i) { hash=(hash^uint8_t(p[i]))*16777619u; hash=(hash^uint8_t(p[i]>>8))*16777619u; }
    return hash;
}
} // namespace lab
