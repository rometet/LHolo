#pragma once

#include <cmath>
#include <limits>

namespace lholo::place::detail {

struct VoxelRayAxis { int step{}; double nextBoundary{}; double delta{}; };

[[nodiscard]] inline VoxelRayAxis voxelRayAxis(float origin, int cell, float direction) noexcept {
    if (direction == 0.0f) {
        auto const infinity = (std::numeric_limits<double>::infinity)();
        return {0, infinity, infinity};
    }
    auto const step = direction > 0.0f ? 1 : -1;
    auto const delta = std::abs(1.0 / static_cast<double>(direction));
    auto const next = (step > 0 ? (static_cast<double>(cell) + 1.0 - origin)
                               : (static_cast<double>(origin) - cell)) * delta;
    return {step, next, delta};
}

} // namespace lholo::place::detail
