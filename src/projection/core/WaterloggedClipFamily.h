// LHolo - safe shape families for native waterlogged face clipping.
// Minecraft-free. Keep cutout/transparent trapdoors out: their texture alpha
// holes are not represented in the rectangular native body geometry.
#pragma once
#include <cstdint>
#include <string_view>
namespace lholo::projection::detail {
enum class WaterloggedClipFamily : std::uint8_t {
    None, Stairs, Slab, Trapdoor
};
[[nodiscard]] constexpr WaterloggedClipFamily waterloggedClipCandidate(
    std::string_view blockName
) noexcept {
    if (!blockName.starts_with("minecraft:"))
        return WaterloggedClipFamily::None;
    auto const name = blockName.substr(sizeof("minecraft:") - 1U);
    if (name.ends_with("_stairs")) return WaterloggedClipFamily::Stairs;
    if (name.ends_with("_slab")
        || name == "stone_block_slab2" || name == "stone_block_slab3"
        || name == "stone_block_slab4")
        return WaterloggedClipFamily::Slab;
    if (name == "trapdoor" || name.ends_with("_trapdoor"))
        return WaterloggedClipFamily::Trapdoor;
    return WaterloggedClipFamily::None;
}
// Opacity from the real engine's chosen RenderBucket, never a family guess.
// In particular, cutout trapdoors (often with holes) must keep their water.
[[nodiscard]] constexpr WaterloggedClipFamily waterloggedSafeClipFamily(
    std::string_view blockName,
    bool opaqueNativeLayer
) noexcept {
    return opaqueNativeLayer ? waterloggedClipCandidate(blockName)
                             : WaterloggedClipFamily::None;
}
} // namespace lholo::projection::detail
