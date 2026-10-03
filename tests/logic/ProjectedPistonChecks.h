#pragma once

#include "projection/core/ProjectedPistonAppearance.h"
#include "projection/mesh/ProjectedPistonRenderScope.h"

#include <array>
#include <future>
#include <limits>
#include <memory>
#include <stdexcept>

namespace lholo::tests {

template <class Check>
void runProjectedPistonChecks(Check check) {
    using namespace projection::detail;
    for (auto const opacity : {0.0f, 0.15f, 0.5f, 1.0f}) {
        check(projectedPistonPass(opacity, false) == (opacity == 1.0f
            ? ProjectedPistonPass::Native : ProjectedPistonPass::Skip));
        check(projectedPistonPass(opacity, true) == (opacity == 0.0f
            ? ProjectedPistonPass::Skip : opacity == 1.0f
            ? ProjectedPistonPass::Native : ProjectedPistonPass::Ghost));
    }
    check(projectedPistonPass(-1.0f, true) == ProjectedPistonPass::Skip);
    check(projectedPistonPass(2.0f, false) == ProjectedPistonPass::Native);
    check(projectedPistonPass(std::numeric_limits<float>::quiet_NaN(), false)
        == ProjectedPistonPass::Native);

    struct Color { float r, g, b, a; };
    struct Material { bool blend; bool depthTest; int depthWrite; };
    auto native = std::make_shared<Material>(Material{false, true, 1});
    auto blend = std::make_shared<Material>(Material{true, true, 0});
    auto modelMaterial = native;
    Color color{0.3f, 0.5f, 0.7f, 0.8f};
    bool dirty = false;
    {
        ScopedPistonAppearance appearance{modelMaterial, blend, color, dirty, 0.5f};
        check(modelMaterial == blend && color.a == 0.4f && dirty);
        check(color.r == 0.3f && color.g == 0.5f && color.b == 0.7f);
        {
            ScopedPistonAppearance nested{modelMaterial, blend, color, dirty, 0.5f};
            check(color.a == 0.2f);
        }
        check(modelMaterial == blend && color.a == 0.4f);
    }
    check(modelMaterial == native && color.a == 0.8f && dirty);
    dirty = false;
    try {
        ScopedPistonAppearance appearance{modelMaterial, blend, color, dirty, 0.15f};
        throw std::runtime_error("native submit failure");
    } catch (std::runtime_error const&) { }
    check(modelMaterial == native && color.a == 0.8f && dirty);
    // No writes to shared materials, including their depth behavior.
    check(!native->blend && native->depthTest && native->depthWrite == 1);
    check(blend->blend && blend->depthTest && blend->depthWrite == 0);

    ProjectedPistonRender const first{nullptr, nullptr, 0.15f};
    ProjectedPistonRender const second{nullptr, nullptr, 0.5f};
    check(activeProjectedPistonRender == nullptr);
    {
        ScopedProjectedPistonRender scope{&first};
        check(activeProjectedPistonRender == &first);
        check(std::async(std::launch::async, [] {
            return activeProjectedPistonRender == nullptr;
        }).get());
        try {
            ScopedProjectedPistonRender nested{&second};
            check(activeProjectedPistonRender == &second);
            throw std::runtime_error("dispatcher failure");
        } catch (std::runtime_error const&) { }
        check(activeProjectedPistonRender == &first);
        {
            ScopedProjectedPistonRender vanilla{nullptr};
            check(activeProjectedPistonRender == nullptr);
        }
        check(activeProjectedPistonRender == &first);
    }
    check(activeProjectedPistonRender == nullptr);
}

} // namespace lholo::tests
