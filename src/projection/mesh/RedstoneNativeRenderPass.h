// Native extra render-pass classification for the two diode block families.
// Kept independent of SDK types so the policy is covered by headless tests.
#pragma once
#include <string_view>

namespace lholo::projection::detail {

[[nodiscard]] constexpr bool isRedstoneDiodeNativeModel(
    std::string_view name
) noexcept {
    return name == "minecraft:unpowered_repeater"
        || name == "minecraft:powered_repeater"
        || name == "minecraft:unpowered_comparator"
        || name == "minecraft:powered_comparator";
}

// The engine cache may retain geometry calculated for a different
// position even when the serialized diode state is identical. Diodes are
// inexpensive to tessellate and depend on neighboring block context.
[[nodiscard]] constexpr bool useNativeBlockCalculationCache(
    std::string_view name
) noexcept {
    return !isRedstoneDiodeNativeModel(name);
}

[[nodiscard]] constexpr bool diodeNeedsDoubleSidedGhostMaterial(
    std::string_view name, bool nativeLayerIsSingleSided
) noexcept {
    return nativeLayerIsSingleSided && isRedstoneDiodeNativeModel(name);
}

[[nodiscard]] constexpr bool diodeNeedsNativeExtraFlag(
    std::string_view name,
    bool isAdditionalRenderLayer
) noexcept {
    return isAdditionalRenderLayer && isRedstoneDiodeNativeModel(name);
}

} // namespace lholo::projection::detail
