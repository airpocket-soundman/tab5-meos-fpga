// Adapted for native MeOS; provenance and changes: third_party/gpu_lab/README.md
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <utility>


namespace integer_raster {
// Wide API fields ensure invalid values can be rejected before narrowing.
struct Vertex { std::int64_t x, y, z; };
using Scissor = std::array<std::int64_t, 4>;


inline std::int64_t floor16(std::int64_t v) {
    return v >= 0 ? v / 16 : -((-v + 15) / 16);
}
inline std::int64_t edge(const Vertex& a, const Vertex& b, std::int64_t x, std::int64_t y) {
    return (b.x-a.x)*(y-a.y) - (b.y-a.y)*(x-a.x);
}
inline bool inclusive(const Vertex& a, const Vertex& b) {
    const auto dy = b.y-a.y, dx = b.x-a.x;
    return dy < 0 || (dy == 0 && dx > 0);
}

inline void invalid(const char* message) {
#if defined(__cpp_exceptions)
    throw std::invalid_argument(message);
#else
    (void)message;
    std::abort();  // Embedded callers prevalidate their bounded generated commands.
#endif
}

class FrameView {
public:
    const std::int64_t width, height;
    std::uint16_t *const color, *const depth;

    FrameView(std::int64_t w, std::int64_t h, std::uint16_t* c, std::uint16_t* d)
        : width(w), height(h), color(c), depth(d) {
        if (w < 1 || w > 640 || h < 1 || h > 360)
            invalid("unsupported dimensions");
        if (!c || !d) invalid("null target");
    }
    void clear(std::int64_t c = 0, std::int64_t z = 0) {
        validate_u16(c); validate_u16(z);
        std::fill(color, color+width*height, static_cast<std::uint16_t>(c));
        std::fill(depth, depth+width*height, static_cast<std::uint16_t>(z));
    }
    void line(Vertex a, Vertex b, std::int64_t c,
                  std::optional<Scissor> scissor = std::nullopt, bool /*collect_coverage*/ = true) {
        validate_vertex(a); validate_vertex(b);
        const auto s = validate_draw(c, scissor);
        auto x = floor16(a.x), y = floor16(a.y);
        const auto x1 = floor16(b.x), y1 = floor16(b.y);
        const auto dx = x1 >= x ? x1-x : x-x1;
        const auto dy = -(y1 >= y ? y1-y : y-y1);
        const std::int64_t sx = x < x1 ? 1 : -1, sy = y < y1 ? 1 : -1;
        auto err = dx+dy;
        for (;;) {
            if (s[0] <= x && x < s[2] && s[1] <= y && y < s[3]) {
                const auto i = static_cast<std::size_t>(y*width+x);
                color[i] = static_cast<std::uint16_t>(c);

            }
            if (x == x1 && y == y1) return;
            const auto twice = 2*err;
            if (twice >= dy) { err += dy; x += sx; }
            if (twice <= dx) { err += dx; y += sy; }
        }
    }
    void triangle(Vertex a, Vertex b, Vertex c, std::int64_t rgb,
                      std::optional<Scissor> scissor = std::nullopt, bool /*collect_coverage*/ = true) {
        validate_vertex(a); validate_vertex(b); validate_vertex(c);
        const auto s = validate_draw(rgb, scissor);
        auto area = edge(a,b,c.x,c.y);
        if (!area) return;
        if (area < 0) { std::swap(b,c); area = -area; }
        const auto y0 = std::max(s[1],floor16(std::min({a.y,b.y,c.y})));
        const auto y1 = std::min(s[3],floor16(std::max({a.y,b.y,c.y}))+1);
        const auto x0 = std::max(s[0],floor16(std::min({a.x,b.x,c.x})));
        const auto x1 = std::min(s[2],floor16(std::max({a.x,b.x,c.x}))+1);
        for (auto y=y0; y<y1; ++y) for (auto x=x0; x<x1; ++x) {
            const auto px=x*16+8, py=y*16+8;
            const auto wa=edge(b,c,px,py), wb=edge(c,a,px,py), wc=edge(a,b,px,py);
            if (!inside(wa,b,c) || !inside(wb,c,a) || !inside(wc,a,b)) continue;
            const auto i=static_cast<std::size_t>(y*width+x);

            const auto z=(wa*a.z+wb*b.z+wc*c.z)/area;
            if (z > depth[i]) {
                depth[i]=static_cast<std::uint16_t>(z);
                color[i]=static_cast<std::uint16_t>(rgb);
            }
        }
        return;
    }
private:
    static bool inside(std::int64_t w, const Vertex& a, const Vertex& b) {
        return w > 0 || (w == 0 && inclusive(a,b));
    }
    static void validate_u16(std::int64_t v) {
        if (v < 0 || v > 65535) invalid("uint16 out of range");
    }
    static void validate_vertex(const Vertex& v) {
        if (v.x < -32768 || v.x > 32767 || v.y < -32768 || v.y > 32767)
            invalid("coordinate out of range");
        validate_u16(v.z);
    }
    Scissor validate_draw(std::int64_t c, std::optional<Scissor> clip) const {
        validate_u16(c);
        const auto s=clip.value_or(Scissor{0,0,width,height});
        if (!(0 <= s[0] && s[0] <= s[2] && s[2] <= width &&
              0 <= s[1] && s[1] <= s[3] && s[3] <= height))
            invalid("scissor out of range");
        return s;
    }
};

}  // namespace integer_raster
