// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026 MarmieQi
// Conservative, Minecraft-free clipping of waterlogged liquid faces against
// native opaque body quads. No geometry is invented for unmatched faces.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace lholo::projection::detail {

struct WaterloggedFaceRect {
    int axis{}; // fixed coordinate axis: x=0, y=1, z=2
    float plane{};
    float u0{}, u1{};
    float v0{}, v1{};
};

inline constexpr float WaterloggedPlaneEpsilon = 0.002F;
inline constexpr float WaterloggedEdgeEpsilon = 0.002F;
inline constexpr float WaterloggedMinimumArea = 0.00001F;
inline constexpr std::size_t WaterloggedMaxBodyRects = 48U;
inline constexpr std::size_t WaterloggedMaxPieces = 12U;
using WaterloggedQuadPositions = std::array<std::array<float, 3>, 4>;

[[nodiscard]] inline std::optional<WaterloggedFaceRect> waterloggedAxisRect(
    WaterloggedQuadPositions const& positions
) noexcept {
    for (auto const& point : positions)
        for (float coordinate : point)
            if (!std::isfinite(coordinate)) return std::nullopt;

    for (int axis = 0; axis < 3; ++axis) {
        auto const a = (axis + 1) % 3;
        auto const b = (axis + 2) % 3;
        auto plane = positions[0][axis];
        float u0 = positions[0][a], u1 = u0;
        float v0 = positions[0][b], v1 = v0;
        bool planar = true;
        for (auto const& pos : positions) {
            planar &= std::fabs(pos[axis] - plane) <= 0.0004F;
            u0 = std::min(u0, pos[a]);
            u1 = std::max(u1, pos[a]);
            v0 = std::min(v0, pos[b]);
            v1 = std::max(v1, pos[b]);
        }
        if (!planar || u1 - u0 <= WaterloggedEdgeEpsilon
            || v1 - v0 <= WaterloggedEdgeEpsilon) continue;
        // Each corner must be one of the four corners of a single rectangle.
        unsigned const masksExpected = 0xFU;
        unsigned masks{};
        for (auto const& pos : positions) {
            bool const uLow = std::fabs(pos[a] - u0) <= 0.0004F;
            bool const uHigh = std::fabs(pos[a] - u1) <= 0.0004F;
            bool const vLow = std::fabs(pos[b] - v0) <= 0.0004F;
            bool const vHigh = std::fabs(pos[b] - v1) <= 0.0004F;
            if ((!uLow && !uHigh) || (!vLow && !vHigh)) {
                masks = 0;
                break;
            }
            masks |= 1U << ((uHigh ? 1U : 0U) + (vHigh ? 2U : 0U));
        }
        if (masks == masksExpected)
            return WaterloggedFaceRect{axis, plane, u0, u1, v0, v1};
    }
    return std::nullopt;
}

[[nodiscard]] inline float waterloggedSnap(float edge, float boundary) noexcept {
    return std::fabs(edge - boundary) <= WaterloggedEdgeEpsilon ? boundary : edge;
}

[[nodiscard]] inline float waterloggedRectArea(WaterloggedFaceRect const& rect) noexcept {
    return std::max(0.F, rect.u1 - rect.u0) * std::max(0.F, rect.v1 - rect.v0);
}

// Nullopt: no reliable body/body-facing plane overlap (keep native quad).
// Empty vector: the entire water quad was covered by the same cell's solid
// body face(s). Nonempty vector: exact surviving rectangular fragments.
// Fail closed on invalid, diagonal, overly complex or tiny faces.
[[nodiscard]] inline std::optional<std::vector<WaterloggedFaceRect>>
clipWaterloggedNativeFace(
    WaterloggedQuadPositions const& water,
    std::span<WaterloggedFaceRect const> bodyFaces
) {
    auto const source = waterloggedAxisRect(water);
    if (!source || bodyFaces.empty() || bodyFaces.size() > WaterloggedMaxBodyRects)
        return std::nullopt;
    std::vector<WaterloggedFaceRect> pieces{*source};
    bool changed{};
    for (auto const& body : bodyFaces) {
        if (body.axis != source->axis
            || !std::isfinite(body.plane)
            || std::fabs(body.plane - source->plane) > WaterloggedPlaneEpsilon
            || !std::isfinite(body.u0) || !std::isfinite(body.u1)
            || !std::isfinite(body.v0) || !std::isfinite(body.v1)
            || body.u1 <= body.u0 || body.v1 <= body.v0) {
            continue;
        }
        std::vector<WaterloggedFaceRect> next;
        for (auto const& part : pieces) {
            auto u0 = waterloggedSnap(body.u0, part.u0);
            auto u1 = waterloggedSnap(body.u1, part.u1);
            auto v0 = waterloggedSnap(body.v0, part.v0);
            auto v1 = waterloggedSnap(body.v1, part.v1);
            u0 = std::clamp(u0, part.u0, part.u1);
            u1 = std::clamp(u1, part.u0, part.u1);
            v0 = std::clamp(v0, part.v0, part.v1);
            v1 = std::clamp(v1, part.v0, part.v1);
            if (u1 - u0 <= WaterloggedEdgeEpsilon ||
                v1 - v0 <= WaterloggedEdgeEpsilon) {
                next.push_back(part);
                continue;
            }
            changed = true;
            auto add = [&](float loU, float hiU, float loV, float hiV) {
                WaterloggedFaceRect fragment{part.axis, part.plane, loU, hiU, loV, hiV};
                if (waterloggedRectArea(fragment) > WaterloggedMinimumArea)
                    next.push_back(fragment);
            };
            add(part.u0, u0, part.v0, part.v1);
            add(u1, part.u1, part.v0, part.v1);
            add(u0, u1, part.v0, v0);
            add(u0, u1, v1, part.v1);
        }
        if (next.size() > WaterloggedMaxPieces) return std::nullopt;
        pieces = std::move(next);
        if (pieces.empty()) break;
    }
    return changed ? std::make_optional(std::move(pieces)) : std::nullopt;
}

// Return a clipped 3D point that preserves the source face's depth offset
// and vertex winding. Preserve original vertices when there is no clipping.
[[nodiscard]] inline std::array<float, 3> waterloggedCorner(
    WaterloggedFaceRect const& source,
    WaterloggedFaceRect const& part,
    std::array<float, 3> const& corner
) noexcept {
    auto result = corner;
    auto const a = (source.axis + 1) % 3;
    auto const b = (source.axis + 2) % 3;
    result[a] = std::fabs(corner[a] - source.u0) <= WaterloggedEdgeEpsilon ? part.u0 : part.u1;
    result[b] = std::fabs(corner[b] - source.v0) <= WaterloggedEdgeEpsilon ? part.v0 : part.v1;
    return result;
}

// Rectangle parameter in the unchanged source quad for UV/color interpolation.
[[nodiscard]] inline std::array<float, 2> waterloggedFraction(
    WaterloggedFaceRect const& source,
    std::array<float, 3> const& point
) noexcept {
    auto a = (source.axis + 1) % 3;
    auto b = (source.axis + 2) % 3;
    return {
        std::clamp((point[a] - source.u0) / (source.u1 - source.u0), 0.F, 1.F),
        std::clamp((point[b] - source.v0) / (source.v1 - source.v0), 0.F, 1.F)
    };
}

} // namespace lholo::projection::detail
