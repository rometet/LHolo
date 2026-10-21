// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Pure guards used by the cosmetic liquid proxy fallback.  The native liquid
// and Praxis Exact Replay paths do not depend on this header.

#pragma once

#include <cmath>

namespace lholo::projection::detail {

struct LiquidProxyUvRect {
    float u0{};
    float v0{};
    float u1{};
    float v1{};
};

[[nodiscard]] inline bool isValidLiquidProxyUvRect(LiquidProxyUvRect const& rect) noexcept {
    return std::isfinite(rect.u0) && std::isfinite(rect.v0)
        && std::isfinite(rect.u1) && std::isfinite(rect.v1)
        && rect.u1 > rect.u0 && rect.v1 > rect.v0;
}

// A transparent block must not hide a proxy face.  Only a same-kind liquid or
// a full opaque block is a complete shared-face occluder; panes, stained glass,
// slabs and other partial/transparent bodies remain visible through the liquid.
[[nodiscard]] constexpr bool liquidProxyFaceOccluded(
    bool opaqueFullBlock,
    bool sameLiquid
) noexcept {
    return opaqueFullBlock || sameLiquid;
}

} // namespace lholo::projection::detail
