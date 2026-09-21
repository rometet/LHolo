// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Pure UV0 remapping used by the retained native-liquid experiment. The
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

        auto const spanU = maxU - minU;
        auto const spanV = maxV - minV;
        for (std::size_t corner = 0; corner < 4U; ++corner) {
            auto& uv = uvs[first + corner];
            auto normalizedU = canonicalCorners[corner][0];
            auto normalizedV = canonicalCorners[corner][1];
            if (spanU > epsilon) {
                normalizedU = std::clamp((uv.x - minU) / spanU, 0.0f, 1.0f);
            }
            if (spanV > epsilon) {
                normalizedV = std::clamp((uv.y - minV) / spanV, 0.0f, 1.0f);
            }
            uv.x = rect.u0 + normalizedU * (rect.u1 - rect.u0);
            uv.y = rect.v0 + normalizedV * (rect.v1 - rect.v0);
        }
    }
    if (diagnostics) diagnostics->remappedVertices = uvs.size();
    return true;
}

} // namespace lholo::projection::detail
