// Pure bounded virtual-air policy for schematic diode tessellation.
// Real-world blocks outside the projected fixture must not replace empty
// schematic neighbors while computing the redstone diode native model.
#pragma once
#include <cstdint>

namespace lholo::projection::detail {

[[nodiscard]] constexpr bool inVirtualAirNeighborhood(
    std::int64_t x, std::int64_t y, std::int64_t z,
    std::int64_t centerX, std::int64_t centerY, std::int64_t centerZ,
    std::int64_t radius = 2
) noexcept {
    return radius >= 0
        && x >= centerX - radius && x <= centerX + radius
        && y >= centerY - radius && y <= centerY + radius
        && z >= centerZ - radius && z <= centerZ + radius;
}

} // namespace lholo::projection::detail
