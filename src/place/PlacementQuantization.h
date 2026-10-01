#pragma once

#include <cmath>
#include <cstdint>

namespace lholo::place::detail {

// The caller validates the eye with checkedVoxelRayOrigin first. Its allowed
// int-coordinate domain needs up to 34 bits after quarter-cell quantization.
[[nodiscard]] inline std::int64_t quantizePlacementEye(float value) noexcept {
    return std::llround(static_cast<double>(value) * 4.0);
}

} // namespace lholo::place::detail
