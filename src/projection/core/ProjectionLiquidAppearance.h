// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026 MarmieQi
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lholo::projection::detail {

inline float normalizedLiquidProjectionOpacity(float opacity) noexcept {
    return std::isfinite(opacity) ? std::clamp(opacity, 0.0F, 1.0F) : 1.0F;
}

// Scale only the already-derived alpha. The accepted water seed/Missing RGB
// and 100% water/lava alpha remain exact; native canonical streams stay owned
// by Exact Replay and are never rewritten here.
inline std::uint32_t applyLiquidProjectionOpacity(std::uint32_t packed, float opacity) noexcept {
    auto const normalized = normalizedLiquidProjectionOpacity(opacity);
    if (normalized == 1.0F) return packed;
    auto const alpha = static_cast<std::uint32_t>(std::lround(
        static_cast<float>((packed >> 24U) & 0xFFU) * normalized));
    return (packed & 0x00FFFFFFU) | (alpha << 24U);
}

enum class LiquidProxyFace { Top, Bottom, NorthSouth, EastWest };

// Directional factors from MarmieQi/LHolo e32074b4 (GPL-3.0-or-later).
// Proxy vertices have no native normal/light stream. Bake only fallback RGB;
// alpha and the accepted top tint remain exact. Exact Replay never calls this.
inline std::uint32_t liquidProxyFaceColor(std::uint32_t packed, LiquidProxyFace face) noexcept {
    auto const factor = face == LiquidProxyFace::Bottom ? .60F
        : face == LiquidProxyFace::NorthSouth ? .85F
        : face == LiquidProxyFace::EastWest ? .75F : 1.F;
    auto const channel = [packed, factor](unsigned shift) {
        return static_cast<std::uint32_t>(std::lround(
            static_cast<float>((packed >> shift) & 0xffU) * factor)) << shift;
    };
    return (packed & 0xff000000U) | channel(0) | channel(8) | channel(16);
}

} // namespace lholo::projection::detail
