#include "render/BoxWireframeMesh.h"
#include "render/ScopedShaderColor.h"

#include <array>
#include <cstdio>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>

int runOverlayRenderingTests() {
    using namespace lholo::render;
    int checks{}, failures{};
    auto check = [&](bool value) { ++checks; if (!value) ++failures; };
    using Point = std::array<float, 3>;
    for (auto const& bounds : std::array<std::pair<Vec3, Vec3>, 3>{{
        {{0,0,0}, {1,1,1}}, {{0,0,0}, {32,8,16}},
        {{-64,-16,-32}, {16,32,64}}
    }}) {
        auto const vertices = boxWireframeVertices(bounds.first, bounds.second);
        std::map<Point, int> degree;
        std::set<std::pair<Point, Point>> edges;
        std::array<int, 3> axisEdges{};
        Point minimum{bounds.first.x, bounds.first.y, bounds.first.z};
        Point maximum{bounds.second.x, bounds.second.y, bounds.second.z};
        for (std::size_t index = 0; index < vertices.size(); index += 2) {
            Point a{vertices[index].x, vertices[index].y, vertices[index].z};
            Point b{vertices[index+1].x, vertices[index+1].y, vertices[index+1].z};
            int changedAxes{};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                check((a[axis] == minimum[axis] || a[axis] == maximum[axis])
                      && (b[axis] == minimum[axis] || b[axis] == maximum[axis]));
                if (a[axis] != b[axis]) { ++changedAxes; ++axisEdges[axis]; }
            }
            check(changedAxes == 1);
            ++degree[a]; ++degree[b];
            if (b < a) std::swap(a, b);
            check(edges.emplace(a, b).second);
        }
        check(edges.size() == 12 && degree.size() == 8);
        for (auto const& [point, count] : degree) check(count == 3);
        for (auto count : axisEdges) check(count == 4);
    }

    static_assert(!std::is_copy_constructible_v<ScopedShaderColor>);
    static_assert(!std::is_move_constructible_v<ScopedShaderColor>);
    ShaderColor shader{};
    shader.color = mce::Color{0.2f, 0.3f, 0.4f, 0.5f};
    shader.dirty = false;
    auto isColor = [&](float r, float g, float b, float a) {
        auto const& color = shader.color.get();
        return color.r == r && color.g == g && color.b == b && color.a == a;
    };
    {
        ScopedShaderColor cyan{shader, 0xFFFFD633U};
        check(isColor(51.f/255.f, 214.f/255.f, 1.f, 1.f));
        check(shader.dirty);
        shader.dirty = false; // simulate a draw synchronizing the GPU constant
        {
            ScopedShaderColor neutral{shader};
            check(isColor(1,1,1,1) && shader.dirty);
            shader.dirty = false;
        }
        check(isColor(51.f/255.f, 214.f/255.f, 1.f, 1.f) && shader.dirty);
        shader.dirty = false;
    }
    check(isColor(0.2f,0.3f,0.4f,0.5f) && shader.dirty);
    try {
        ScopedShaderColor red{shader, 0xFF0000FFU};
        check(isColor(1,0,0,1));
        shader.dirty = false;
        throw std::runtime_error("draw failed");
    } catch (std::runtime_error const&) {}
    check(isColor(0.2f,0.3f,0.4f,0.5f) && shader.dirty);
    {
        ScopedShaderColor alpha{shader, 0x400000FFU};
        check(isColor(1,0,0,64.f/255.f));
    }
    check(isColor(0.2f,0.3f,0.4f,0.5f) && shader.dirty);
    std::printf("OverlayRenderingTests: %d checks, %d failures\n", checks, failures);
    return failures;
}
