// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Pure UV0 remapping shared by retained and Praxis compatibility liquids. The
// caller supplies the typed atlas rectangle obtained from BlockGraphics; this
// helper has no atlas lookup, renderer ownership or Minecraft ABI dependency.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>

namespace lholo::projection::detail {

struct NativeLiquidAtlasRect {
    float u0{};
    float v0{};
    float u1{};
    float v1{};
};

struct NativeLiquidUvRemapDiagnostics {
    float       firstQuadMinU{};
    float       firstQuadMaxU{};
    float       firstQuadMinV{};
    float       firstQuadMaxV{};
    std::size_t remappedVertices{};
};

inline bool isValidNativeLiquidAtlasRect(NativeLiquidAtlasRect const& rect) {
    return std::isfinite(rect.u0) && std::isfinite(rect.v0)
        && std::isfinite(rect.u1) && std::isfinite(rect.v1)
        && rect.u1 > rect.u0 && rect.v1 > rect.v0;
}

template <class Uv, std::size_t Extent>
bool remapNativeLiquidUvToAtlas(
    std::span<Uv, Extent>                   uvs,
    NativeLiquidAtlasRect const&            rect,
    NativeLiquidUvRemapDiagnostics* diagnostics = nullptr
) {
    if (!isValidNativeLiquidAtlasRect(rect) || uvs.empty() || (uvs.size() % 4U) != 0U) {
        return false;
    }
    for (auto const& uv : uvs) {
        if (!std::isfinite(uv.x) || !std::isfinite(uv.y)) return false;
    }

    constexpr float epsilon = 0.000001f;
    constexpr std::array<std::array<float, 2>, 4> canonicalCorners{{
        {{0.0f, 0.0f}},
        {{1.0f, 0.0f}},
        {{1.0f, 1.0f}},
        {{0.0f, 1.0f}},
    }};
    auto const mapCoordinate = [](float value, float minimum, float maximum,
                                  float targetMinimum, float targetMaximum, float fallback) {
        auto const span = maximum - minimum;
        auto normalized = fallback;
        if (span > epsilon) {
            if (std::isfinite(span)) {
                normalized = std::clamp((value - minimum) / span, 0.0f, 1.0f);
            } else {
                normalized = static_cast<float>(std::clamp(
                    (static_cast<double>(value) - minimum) / (static_cast<double>(maximum) - minimum),
                    0.0, 1.0
                ));
            }
        }
        auto const targetSpan = targetMaximum - targetMinimum;
        // Preserve the original float arithmetic for ordinary native atlas
        // data. Finite endpoints can still have an infinite float difference;
        // widen only that case so 0*inf and inf/inf never produce a UV NaN.
        if (std::isfinite(targetSpan)) return targetMinimum + normalized * targetSpan;
        auto const mapped = static_cast<double>(targetMinimum)
            + normalized * (static_cast<double>(targetMaximum) - targetMinimum);
        return static_cast<float>(std::clamp(mapped,
            static_cast<double>(targetMinimum), static_cast<double>(targetMaximum)));
    };
    for (std::size_t quad = 0; quad < uvs.size() / 4U; ++quad) {
        auto const first = quad * 4U;
        float minU = std::numeric_limits<float>::max();
        float minV = std::numeric_limits<float>::max();
        float maxU = std::numeric_limits<float>::lowest();
        float maxV = std::numeric_limits<float>::lowest();
        for (std::size_t corner = 0; corner < 4U; ++corner) {
            auto const& uv = uvs[first + corner];
            minU = std::min(minU, uv.x);
            minV = std::min(minV, uv.y);
            maxU = std::max(maxU, uv.x);
            maxV = std::max(maxV, uv.y);
        }
        if (quad == 0U && diagnostics) {
            diagnostics->firstQuadMinU = minU;
            diagnostics->firstQuadMaxU = maxU;
            diagnostics->firstQuadMinV = minV;
            diagnostics->firstQuadMaxV = maxV;
        }

        for (std::size_t corner = 0; corner < 4U; ++corner) {
            auto& uv = uvs[first + corner];
            uv.x = mapCoordinate(uv.x, minU, maxU, rect.u0, rect.u1, canonicalCorners[corner][0]);
            uv.y = mapCoordinate(uv.y, minV, maxV, rect.v0, rect.v1, canonicalCorners[corner][1]);
        }
    }
    if (diagnostics) diagnostics->remappedVertices = uvs.size();
    return true;
}

} // namespace lholo::projection::detail
