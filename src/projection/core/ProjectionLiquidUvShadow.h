// Candidate D0: bounded, read-only comparisons of liquid UV atlas targets.
#pragma once

#include "projection/core/ProjectionLiquidUv.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace lholo::projection::detail {

inline constexpr float LiquidUvShadowEpsilon = 0.0000001f;
inline constexpr std::size_t LiquidUvShadowMaxRects = 64;
inline constexpr std::size_t LiquidUvShadowMaxCapturedVertices = 240;
inline constexpr std::array LiquidUvWaterStillAliases{
    "water_still_grey", "still_water_grey", "water_still", "still_water"
};

struct LiquidUvRectComparison {
    std::array<float, 4> delta{};
    float maxAbsDelta{};
    bool exactEqual{};
    bool epsilonEqual{};
};

inline LiquidUvRectComparison compareLiquidUvRects(
    NativeLiquidAtlasRect const& current,
    NativeLiquidAtlasRect const& old
) {
    LiquidUvRectComparison result{};
    result.delta = {
        std::fabs(current.u0 - old.u0),
        std::fabs(current.v0 - old.v0),
        std::fabs(current.u1 - old.u1),
        std::fabs(current.v1 - old.v1),
    };
    result.maxAbsDelta = *std::max_element(result.delta.begin(), result.delta.end());
    result.exactEqual = result.maxAbsDelta == 0.0f;
    result.epsilonEqual = result.maxAbsDelta <= LiquidUvShadowEpsilon;
    return result;
}

inline bool liquidUvTexturePathMatchesAlias(
    std::string_view path,
    std::string_view alias
) {
    auto const lastSlash = path.find_last_of("/\\");
    if (lastSlash != std::string_view::npos) path.remove_prefix(lastSlash + 1);
    constexpr std::string_view png = ".png";
    if (path.size() > png.size() && path.substr(path.size() - png.size()) == png) {
        path.remove_suffix(png.size());
    }
    return path == alias;
}

struct BoundedLiquidUvRectSet {
    std::array<NativeLiquidAtlasRect, LiquidUvShadowMaxRects> rects{};
    std::size_t size{};
    bool saturated{};

    void add(NativeLiquidAtlasRect const& rect) {
        for (std::size_t index = 0; index < size; ++index) {
            if (compareLiquidUvRects(rects[index], rect).exactEqual) return;
        }
        if (size == rects.size()) {
            saturated = true;
            return;
        }
        rects[size++] = rect;
    }
};

} // namespace lholo::projection::detail
