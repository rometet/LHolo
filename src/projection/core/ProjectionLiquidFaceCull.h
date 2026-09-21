// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Pure geometry rules for Phase 3C native-liquid internal-face culling.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <unordered_map>
#include <vector>

namespace lholo::projection::detail {

inline constexpr float NativeLiquidFullFaceTolerance = 0.0025F;

struct NativeLiquidFaceCullMask {
    bool                      valid{};
    std::vector<std::uint8_t> removeQuads;
    std::size_t               facePairs{};

    [[nodiscard]] std::size_t removedVertices() const noexcept {
        return facePairs * 8U;
    }
};

inline bool nativeLiquidPerVertexFieldCountsMatch(
    std::size_t                   vertexCount,
    std::span<std::size_t const> fieldCounts
) noexcept {
    for (auto const count : fieldCounts) {
        if (count != 0U && count != vertexCount) return false;
    }
    return true;
}

namespace liquid_face_cull_detail {

struct FaceKey {
    int               axis{};
    long long         plane{};
    long long         first{};
    long long         second{};

    bool operator==(FaceKey const&) const noexcept = default;
};

struct FaceKeyHash {
    std::size_t operator()(FaceKey const& key) const noexcept {
        std::size_t hash = static_cast<std::size_t>(key.axis + 1);
        auto const mix = [&hash](long long value) {
            hash ^= std::hash<long long>{}(value) + 0x9E3779B97F4A7C15ULL
                + (hash << 6U) + (hash >> 2U);
        };
        mix(key.plane);
        mix(key.first);
        mix(key.second);
        return hash;
    }
};

struct FaceWindings {
    std::size_t positive{std::numeric_limits<std::size_t>::max()};
    std::size_t negative{std::numeric_limits<std::size_t>::max()};
    bool        duplicatePositive{};
    bool        duplicateNegative{};
};

template <class Point>
[[nodiscard]] std::array<float, 3> coordinates(Point const& point) noexcept {
    return {
        static_cast<float>(point.x),
        static_cast<float>(point.y),
        static_cast<float>(point.z)
    };
}

} // namespace liquid_face_cull_detail

// Build a quad-level removal mask. Only a unique + / - winding pair sharing
// the same integer, axis-aligned unit face is eligible. Ambiguous same-facing
// duplicates deliberately keep every face.
template <class Point>
[[nodiscard]] NativeLiquidFaceCullMask buildNativeLiquidInternalFaceCullMask(
    std::span<Point const> positions,
    float                  tolerance = NativeLiquidFullFaceTolerance
) {
    NativeLiquidFaceCullMask result{};
    if (positions.size() < 8U || positions.size() % 4U != 0U
        || !std::isfinite(tolerance) || tolerance <= 0.0F) {
        return result;
    }

    auto const quadCount = positions.size() / 4U;
    result.removeQuads.assign(quadCount, std::uint8_t{0});
    std::unordered_map<
        liquid_face_cull_detail::FaceKey,
        liquid_face_cull_detail::FaceWindings,
        liquid_face_cull_detail::FaceKeyHash>
        faces;
    faces.reserve(quadCount);

    auto const near = [tolerance](float lhs, float rhs) noexcept {
        return std::fabs(lhs - rhs) <= tolerance;
    };
    auto const nearInteger = [near](float value, long long& rounded) noexcept {
        if (!std::isfinite(value)) return false;
        rounded = std::llround(value);
        return near(value, static_cast<float>(rounded));
    };
    constexpr auto NoQuad = std::numeric_limits<std::size_t>::max();

    for (std::size_t quad = 0; quad < quadCount; ++quad) {
        std::array<std::array<float, 3>, 4> points{};
        std::array<float, 3> minimum{
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()
        };
        std::array<float, 3> maximum{
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest()
        };
        for (std::size_t corner = 0; corner < 4U; ++corner) {
            points[corner] = liquid_face_cull_detail::coordinates(
                positions[quad * 4U + corner]
            );
            for (std::size_t axis = 0; axis < 3U; ++axis) {
                if (!std::isfinite(points[corner][axis])) {
                    result.removeQuads.clear();
                    return result;
                }
                minimum[axis] = std::min(minimum[axis], points[corner][axis]);
                maximum[axis] = std::max(maximum[axis], points[corner][axis]);
            }
        }

        int flatAxis = -1;
        for (int axis = 0; axis < 3; ++axis) {
            auto const firstAxis  = (axis + 1) % 3;
            auto const secondAxis = (axis + 2) % 3;
            if (near(minimum[axis], maximum[axis])
                && near(maximum[firstAxis] - minimum[firstAxis], 1.0F)
                && near(maximum[secondAxis] - minimum[secondAxis], 1.0F)) {
                flatAxis = axis;
                break;
            }
        }
        if (flatAxis < 0) continue;

        auto const firstAxis  = (flatAxis + 1) % 3;
        auto const secondAxis = (flatAxis + 2) % 3;
        liquid_face_cull_detail::FaceKey key{};
        key.axis = flatAxis;
        if (!nearInteger(minimum[flatAxis], key.plane)
            || !nearInteger(minimum[firstAxis], key.first)
            || !nearInteger(minimum[secondAxis], key.second)) {
            continue;
        }

        std::array<float, 3> const firstEdge{
            points[1][0] - points[0][0],
            points[1][1] - points[0][1],
            points[1][2] - points[0][2]
        };
        std::array<float, 3> const secondEdge{
            points[2][0] - points[0][0],
            points[2][1] - points[0][1],
            points[2][2] - points[0][2]
        };
        std::array<float, 3> const normal{
            firstEdge[1] * secondEdge[2] - firstEdge[2] * secondEdge[1],
            firstEdge[2] * secondEdge[0] - firstEdge[0] * secondEdge[2],
            firstEdge[0] * secondEdge[1] - firstEdge[1] * secondEdge[0]
        };
        if (std::fabs(normal[flatAxis]) <= tolerance) continue;

        auto& windings = faces[key];
        auto& stored = normal[flatAxis] > 0.0F ? windings.positive : windings.negative;
        auto& duplicate = normal[flatAxis] > 0.0F
            ? windings.duplicatePositive
            : windings.duplicateNegative;
        if (stored == NoQuad) {
            stored = quad;
        } else {
            duplicate = true;
        }
    }

    for (auto const& [key, windings] : faces) {
        (void)key;
        if (windings.positive == NoQuad || windings.negative == NoQuad
            || windings.duplicatePositive || windings.duplicateNegative) {
            continue;
        }
        result.removeQuads[windings.positive] = 1U;
        result.removeQuads[windings.negative] = 1U;
        ++result.facePairs;
    }
    result.valid = true;
    return result;
}

} // namespace lholo::projection::detail
