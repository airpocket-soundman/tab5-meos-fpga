// Adapted for native MeOS; provenance and changes: third_party/gpu_lab/README.md
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>


namespace lab {
constexpr int width = 640, height = 360;
constexpr int scene_count = 8, load_count = 3;
constexpr int warmup_frames = 24, sample_frames = 120;
enum class Scene { Wire, Polygon, Mandelbrot, Julia, Particles, Galaxy, Fire, Flow };
enum class Mode { CPU, FPGA };
struct Request {
    Scene scene = Scene::Wire;
    int load = 0;
    uint32_t frame = 0;
};
struct Target { uint16_t *color; uint16_t *depth; };
struct Work { uint32_t vertices = 0, lines = 0, triangles = 0, iterations = 0; };
const char *name(Scene scene);
const char *load_name(int load);
class Cpu final {
    struct FlowState;
    FlowState *flow_=nullptr;
public:
    Cpu();
    Cpu(const Cpu&)=delete;
    Cpu& operator=(const Cpu&)=delete;
    ~Cpu();
    Mode mode() const { return Mode::CPU; }
    bool available(Scene) const { return true; }
    bool render(const Request &, Target, Work &);
};
class CpuInteger final {
public:
    Mode mode() const { return Mode::CPU; }
    bool available(Scene scene) const { return scene==Scene::Wire || scene==Scene::Polygon; }
    bool render(const Request &, Target, Work &);
};
class Fpga final {
public:
    Mode mode() const { return Mode::FPGA; }
    bool available(Scene) const { return false; }
    bool render(const Request &, Target, Work &) { return false; }
};
struct Result {
    bool valid = false;
    double render_ms = 0, p95_ms = 0, pipeline_ms = 0, fps = 0;
    uint32_t frames = 0;
};
class Samples {
    std::array<double, sample_frames> render_{};
    double pipeline_sum_ = 0;
    size_t count_ = 0;
public:
    void clear();
    void add(double render_ms, double pipeline_ms);
    size_t size() const { return count_; }
    Result finish() const;
};
uint32_t checksum(const uint16_t *pixels);
} // namespace lab
