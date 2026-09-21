// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Pure 26.51 representation of Praxis ExistingCurrent / Missing packed color.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace lholo::projection::detail {

struct PraxisCompatRgba8 {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{};

    constexpr bool operator==(PraxisCompatRgba8 const&) const = default;
};

inline constexpr std::array<float, 3> PraxisMissingTint{0.56F, 0.84F, 1.00F};
inline constexpr float PraxisMissingTintStrength = 0.52F;
inline constexpr PraxisCompatRgba8 PraxisWaterColorSeed{63U, 118U, 228U, 255U};
inline constexpr std::uint8_t PraxisNativeWhiteMinimum = 254U;
inline constexpr std::uint8_t PraxisWaterDerivedAlpha = 160U;

struct PraxisCompatLiquidColorSeedResult {
    std::uint32_t packed{};
    bool          waterSeedApplied{};
};

[[nodiscard]] inline constexpr PraxisCompatRgba8 unpackAbgr(std::uint32_t packed) noexcept {
    return {
        static_cast<std::uint8_t>((packed >> 0U) & 0xFFU),
        static_cast<std::uint8_t>((packed >> 8U) & 0xFFU),
        static_cast<std::uint8_t>((packed >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((packed >> 24U) & 0xFFU)
    };
}

[[nodiscard]] inline constexpr std::uint32_t packAbgr(PraxisCompatRgba8 rgba) noexcept {
    return static_cast<std::uint32_t>(rgba.red)
        | (static_cast<std::uint32_t>(rgba.green) << 8U)
        | (static_cast<std::uint32_t>(rgba.blue) << 16U)
        | (static_cast<std::uint32_t>(rgba.alpha) << 24U);
}

// 26.51 emits effectively-white COLOR0 for native water even though its atlas
// tile is intentionally untinted. Only a water vertex within one byte of white
// receives the vanilla #3F76E4 seed; lava and already-tinted water remain native.
[[nodiscard]] inline constexpr PraxisCompatLiquidColorSeedResult
selectPraxisCompatLiquidColorSeed(
    std::uint32_t nativeSource,
    bool          isWater
) noexcept {
    auto const source = unpackAbgr(nativeSource);
    auto const effectivelyWhite = source.red >= PraxisNativeWhiteMinimum
        && source.green >= PraxisNativeWhiteMinimum
        && source.blue >= PraxisNativeWhiteMinimum;
    if (isWater && effectivelyWhite) {
        return {packAbgr(PraxisWaterColorSeed), true};
    }
    return {nativeSource, false};
}

[[nodiscard]] inline constexpr std::uint32_t applyPraxisCompatLiquidAlpha(
    std::uint32_t derivedPacked,
    bool          isWater
) noexcept {
    if (!isWater) return derivedPacked;
    return (derivedPacked & 0x00FFFFFFU)
        | (static_cast<std::uint32_t>(PraxisWaterDerivedAlpha) << 24U);
}

[[nodiscard]] inline std::uint32_t applyPraxisCompatMissingAbgr(
    std::uint32_t sourcePacked
) noexcept {
    auto const source = unpackAbgr(sourcePacked);
    std::array<float, 3> rgb{
        static_cast<float>(source.red) / 255.0F,
        static_cast<float>(source.green) / 255.0F,
        static_cast<float>(source.blue) / 255.0F
    };
    auto const intensity = std::max({rgb[0], rgb[1], rgb[2]});
    if (intensity > (1.0F / 255.0F)) {
        for (auto& channel : rgb) channel /= intensity;
    } else {
        rgb = {1.0F, 1.0F, 1.0F};
    }
    for (std::size_t channel = 0; channel < rgb.size(); ++channel) {
        rgb[channel] += (PraxisMissingTint[channel] - rgb[channel])
            * PraxisMissingTintStrength;
    }
    auto const toByte = [](float value) noexcept {
        return static_cast<std::uint8_t>(std::clamp(
            std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F),
            0L,
            255L
        ));
    };
    return packAbgr({toByte(rgb[0]), toByte(rgb[1]), toByte(rgb[2]), 0xFFU});
}

} // namespace lholo::projection::detail
