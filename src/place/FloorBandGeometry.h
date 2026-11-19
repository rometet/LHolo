// LHolo Easy Place forward floor run geometry; no native or packet dependencies.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace lholo::place::detail {

struct FloorBandCell {
    int x;
    int y;
    int z;
    friend constexpr bool operator==(FloorBandCell const&, FloorBandCell const&) = default;
};

// Scan the aimed floor cell, then three more cells FORWARD into the scene.
// Never extend left or right of the crosshair. The player's view determines
// the signed X/Z direction, rather than a fixed world-coordinate offset.
inline constexpr std::array<int, 4> kFloorBandOffsets{0, 1, 2, 3};

[[nodiscard]] inline std::optional<FloorBandCell> floorBandCell(
    FloorBandCell aim, float viewX, float viewZ, int offset
) noexcept {
    if (offset < 0 || offset > 3
        || !std::isfinite(viewX) || !std::isfinite(viewZ)) return std::nullopt;
    if (offset == 0) return aim;
    float const absX = std::abs(viewX), absZ = std::abs(viewZ);
    // At an exactly vertical view there is no stable forward direction.
    if (absX < 1e-6f && absZ < 1e-6f) return std::nullopt;
    bool const forwardX = absX > absZ;
    float const major = forwardX ? viewX : viewZ;
    std::int64_t const signedOffset = major > 0 ? offset : -offset;
    std::int64_t const extended =
        static_cast<std::int64_t>(forwardX ? aim.x : aim.z) + signedOffset;
    if (extended < (std::numeric_limits<int>::min)()
        || extended > (std::numeric_limits<int>::max)()) return std::nullopt;
    if (forwardX) aim.x = static_cast<int>(extended);
    else aim.z = static_cast<int>(extended);
    return aim;
}

// Only a validated vertical pillar can use the same-tick native build path.
// The canonical deepslate block has a pillar_axis state and was previously
// always classified as non-fast, even when the underlying support's top face
// guarantees the requested Y axis. Reject horizontal logs / unrelated blocks.
[[nodiscard]] constexpr bool floorNativeVerticalDeepslate(
    std::string_view blockName,
    std::string_view pillarAxis,
    int clickedFace,
    bool actualSupportDirectlyBelow
) noexcept {
    return blockName == "minecraft:deepslate"
        && pillarAxis == "y"
        && clickedFace == 1 // Facing::Name::Up, validated in native code
        && actualSupportDirectlyBelow;
}

} // namespace lholo::place::detail
