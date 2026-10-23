#pragma once
// A material is not newly required merely because its existing block has
// the wrong direction/shape/state. Verification still marks that block wrong.
// Separate body and liquid identities for waterlogged projection cells.
#include <cstdint>
namespace lholo::structure {
inline constexpr std::uint8_t MaterialBodyPresent = 1u;
inline constexpr std::uint8_t MaterialLiquidPresent = 2u;

[[nodiscard]] constexpr std::uint8_t materialTypePresentMask(
    bool ready,
    bool expectedBody,
    bool actualBodySameType,
    bool expectedLiquid,
    bool actualLiquidSameType
) noexcept {
    if (!ready) return 0u;
    return static_cast<std::uint8_t>(
        (expectedBody && actualBodySameType ? MaterialBodyPresent : 0u) |
        (expectedLiquid && actualLiquidSameType ? MaterialLiquidPresent : 0u)
    );
}
[[nodiscard]] constexpr bool materialStillNeeded(
    std::uint8_t presentMask,bool liquid
) noexcept {
    return (presentMask & (liquid ? MaterialLiquidPresent : MaterialBodyPresent)) == 0u;
}
} // namespace lholo::structure
