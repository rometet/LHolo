// Shader colors are packed ABGR (0xAABBGGRR), not UI-style ARGB.
// Retain the original outer-box blue exactly as used by the native
// placement bounds before the region-outline changes. Only the new
// yellow region tint needs explicit ARGB-to-ABGR conversion.
#pragma once
#include <cstdint>

namespace lholo::render {
constexpr std::uint32_t argbToShaderAbgr(std::uint32_t argb) noexcept {
    return (argb & 0xFF00FF00u)
        | ((argb & 0x00FF0000u) >> 16)
        | ((argb & 0x000000FFu) << 16);
}
// Original native outer bounds shader tint (unchanged from 4c718d0).
inline constexpr std::uint32_t kPlacementOuterBlue=0xFFFFD633u;
inline constexpr std::uint32_t kPlacementRegionYellow=
    argbToShaderAbgr(0xFFFFD54Fu);

static_assert(kPlacementOuterBlue==0xFFFFD633u);
static_assert(kPlacementRegionYellow==0xFF4FD5FFu);
} // namespace lholo::render
