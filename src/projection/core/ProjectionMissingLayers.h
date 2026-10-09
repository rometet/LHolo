// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026 MarmieQi
#pragma once
#include <cstdint>

namespace lholo::projection::detail {
inline constexpr std::uint8_t MissingLayerBody = 1;
inline constexpr std::uint8_t MissingLayerLiquid = 2;
inline constexpr std::uint8_t MissingLayerBoth = MissingLayerBody | MissingLayerLiquid;
constexpr std::uint8_t projectionMissingLayerMask(bool body, bool liquid) noexcept {
    return (body ? MissingLayerBody : 0) | (liquid ? MissingLayerLiquid : 0);
}
template <class Correction>
constexpr bool projectionBodyShouldRender(Correction correction, std::uint8_t missing) noexcept {
    return correction == Correction::Unknown
        || (correction == Correction::Missing && (missing & MissingLayerBody) != 0);
}
template <class Correction>
constexpr bool projectionLiquidShouldRender(Correction correction, std::uint8_t missing) noexcept {
    return correction == Correction::Missing && (missing & MissingLayerLiquid) != 0;
}
} // namespace lholo::projection::detail
