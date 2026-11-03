#pragma once

#include "projection/core/ProjectedPistonAppearance.h"
#include "projection/core/ProjectedPistonPrefetchCulling.h"
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
    // Only mapped piston actors in sections beyond the expanded prefetch
    // frustum are culled. All unknown or non-piston actors fail open.
    std::array<std::size_t, 4> const sectionOf{0, 1, 2, 99};
    std::array<bool, 3> const prefetch{true, false, true};
    std::size_t visibilityChecks{};
    auto mayRender = [&](bool piston, std::size_t block) {
        return projectedPistonMayRender(piston, block, sectionOf, prefetch.size(),
            [&](std::size_t section) { ++visibilityChecks; return prefetch[section]; });
    };
    check(mayRender(true, 0));
    check(!mayRender(true, 1));
    check(mayRender(true, 2));
    check(mayRender(true, 3)); // invalid section index: fail open
    check(mayRender(true, 4)); // missing mapping: fail open
    check(mayRender(false, 1)); // other block-entity types unchanged
    check(visibilityChecks == 3);
    auto const invalidFrustum = ProjectionFrustum{};
    auto const identityFrustum = ProjectionFrustum::fromViewProjection(glm::mat4{1});
    check(projectedPistonCellVisible(invalidFrustum, {}, {100000, 70, -200000}));
    check(projectedPistonCellVisible(identityFrustum, {}, {0, 0, 0}));
    check(projectedPistonCellVisible(identityFrustum, {}, {3, 0, 0}));
    check(!projectedPistonCellVisible(identityFrustum, {}, {10, 0, 0}));
    check(projectedPistonCellVisible(identityFrustum, {1000, 70, -2000}, {1000, 70, -2000}));
    check(!projectedPistonCellVisible(identityFrustum, {1000, 70, -2000}, {1010, 70, -2000}));
    check(projectedPistonCellVisible(identityFrustum, {-100000, -30, 500000},
                                    {-100000, -30, 500000}));
    check(!projectedPistonCellVisible(identityFrustum, {-100000, -30, 500000},
                                     {-99980, -30, 500000}));
    // Perspective view: behind-camera pistons are rejected; a near-edge
    // piston head remains in the padded box and must stay visible.
    glm::mat4 perspective{0};
    perspective[0][0] = perspective[1][1] = 1;
    perspective[2][2] = -1.002f;
    perspective[2][3] = -1;
    perspective[3][2] = -.2f;
    auto const viewFrustum = ProjectionFrustum::fromViewProjection(perspective);
    check(projectedPistonCellVisible(viewFrustum, {}, {0, 0, -15}));
    check(!projectedPistonCellVisible(viewFrustum, {}, {30, 0, -15}));
    check(!projectedPistonCellVisible(viewFrustum, {}, {0, 0, 30}));
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
