#pragma once

#include "projection/core/ProjectionFrustum.h"
#include <cstddef>
#include <span>

namespace lholo::projection::detail {
// Only reject a piston after an authoritative block -> section mapping and
// a conservative (16-block expanded) frustum classification both exist.
// Unknown mappings/sections fail open. No geometry, colors or other block
// entities are affected by this predicate.
template <class IsPrefetchVisible>
[[nodiscard]] bool projectedPistonMayRender(
    bool isPiston,
    std::size_t structureIndex,
    std::span<std::size_t const> blockToSection,
    std::size_t sectionCount,
    IsPrefetchVisible&& prefetchVisible
) {
    if (!isPiston || structureIndex >= blockToSection.size()) return true;
    auto const section = blockToSection[structureIndex];
    return section >= sectionCount || prefetchVisible(section);
}

// For very large schematics the 16-block section prefetch box can include
// thousands of pistons beyond the view cone. Cull their individual models
// against a 3-block-expanded cell box (including full extension of the arm).
// The frustum uses camera-relative coordinates and fails open if invalid.
// This ONLY affects piston block actors, not section terrain or overlays.
inline bool projectedPistonCellVisible(
    ProjectionFrustum const& frustum, Vec3 const& worldCamera,
    Vec3 const& pistonWorldPosition, float padding = 3.0f
) noexcept {
    auto const x = pistonWorldPosition.x - worldCamera.x;
    auto const y = pistonWorldPosition.y - worldCamera.y;
    auto const z = pistonWorldPosition.z - worldCamera.z;
    return frustum.isBoxVisible(
        {x - padding, y - padding, z - padding},
        {x + 1.0f + padding, y + 1.0f + padding, z + 1.0f + padding}
    );
}
} // namespace lholo::projection::detail
