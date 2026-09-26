// Candidate C0: bounded, read-only comparison of the current liquid face mask
// with Praxis-Client@119791c cullCoincidentOpposingFullFaces pairing semantics.
#pragma once

#include "projection/core/ProjectionLiquidFaceCull.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <unordered_map>
#include <vector>

namespace lholo::projection::detail {

inline constexpr std::size_t MaxLiquidCullDiagnosticQuads = 4096U;
inline constexpr std::size_t MaxLiquidCullDetailQuads = 64U;
inline constexpr std::size_t MaxLiquidCullDiffKeys = 32U;

template <class Point>
[[nodiscard]] std::uint64_t liquidCullGeometrySignature(
    std::span<Point const> positions
) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    auto const mix = [&hash](std::uint32_t value) {
        hash = (hash ^ value) * 1099511628211ULL;
    };
    mix(static_cast<std::uint32_t>(positions.size()));
    mix(static_cast<std::uint32_t>(positions.size() >> 32U));
    for (auto const& position : positions) {
        auto const point = liquid_face_cull_detail::coordinates(position);
        for (auto const value : point) mix(std::bit_cast<std::uint32_t>(value));
    }
    return hash;
}

enum class LiquidShadowQuadClass : std::uint8_t {
    FullUnitInteger,
    PartialAxisAligned,
    NonIntegerFull,
    Sloped,
    Unknown
};

struct LiquidShadowQuad {
    std::size_t index{};
    std::array<std::array<float, 3>, 4> vertices{};
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};
    std::array<float, 3> extent{};
    int flatAxis{-1};
    float plane{};
    float firstExtent{};
    float secondExtent{};
    bool axisAligned{};
    bool fullUnit{};
    bool integerPlane{};
    int winding{};
    bool hasKey{};
    liquid_face_cull_detail::FaceKey key{};
    LiquidShadowQuadClass classification{LiquidShadowQuadClass::Unknown};
};

struct LiquidShadowPair {
    liquid_face_cull_detail::FaceKey key{};
    std::size_t firstQuad{};
    std::size_t secondQuad{};
};

struct LiquidShadowDiff {
    liquid_face_cull_detail::FaceKey key{};
    std::size_t firstQuad{};
    std::size_t secondQuad{};
    bool duplicatePositive{};
    bool duplicateNegative{};
};

struct LiquidCullShadowReport {
    bool valid{};
    bool tooLarge{};
    std::size_t vertices{};
    std::size_t quads{};
    std::size_t currentPairs{};
    std::size_t currentRemovedVertices{};
    std::size_t oldPraxisPairs{};
    std::size_t oldPraxisRemovedVertices{};
    std::size_t fullUnitIntegerFaces{};
    std::size_t partialAxisAlignedFaces{};
    std::size_t nonIntegerPlaneFaces{};
    std::size_t slopedFaces{};
    std::size_t unknownFaces{};
    std::size_t eligibleUnitQuads{};
    std::size_t rejectedNonFlat{};
    std::size_t rejectedNonUnit{};
    std::size_t rejectedNonIntegerPlane{};
    std::size_t positiveFaces{};
    std::size_t negativeFaces{};
    std::size_t uniqueFaceKeys{};
    std::size_t keysWithPositive{};
    std::size_t keysWithNegative{};
    std::size_t keysWithOppositeWindings{};
    std::size_t duplicatePositiveKeys{};
    std::size_t duplicateNegativeKeys{};
    std::size_t duplicateBothKeys{};
    std::size_t keysBlockedOnlyByDuplicatePolicy{};
    std::size_t onePositiveOnlyKeys{};
    std::size_t oneNegativeOnlyKeys{};
    std::size_t oneEachKeys{};
    std::size_t multiplePositiveOneNegativeKeys{};
    std::size_t onePositiveMultipleNegativeKeys{};
    std::size_t multipleBothKeys{};
    std::vector<LiquidShadowQuad> detailQuads;
    std::vector<LiquidShadowPair> oldPairs;
    std::vector<LiquidShadowDiff> differences;
};

// Pure diagnostic. It never mutates positions, Tessellator streams, or the
// production removal mask. Streams above the cap are explicitly skipped.
template <class Point>
[[nodiscard]] LiquidCullShadowReport analyzeLiquidCullShadow(
    std::span<Point const> positions,
    float tolerance = NativeLiquidFullFaceTolerance
) {
    using liquid_face_cull_detail::FaceKey;
    using liquid_face_cull_detail::FaceKeyHash;
    constexpr auto NoQuad = std::numeric_limits<std::size_t>::max();
    LiquidCullShadowReport result{};
    result.vertices = positions.size();
    if (positions.size() % 4U != 0U || !std::isfinite(tolerance)
        || tolerance <= 0.0F) return result;
    result.quads = positions.size() / 4U;
    if (result.quads > MaxLiquidCullDiagnosticQuads) {
        result.tooLarge = true;
        return result;
    }
    result.valid = true;
    result.detailQuads.reserve(std::min(result.quads, MaxLiquidCullDetailQuads));
    auto const current = buildNativeLiquidInternalFaceCullMask(positions, tolerance);
    result.currentPairs = current.facePairs;
    result.currentRemovedVertices = current.removedVertices();

    struct KeyCounts {
        std::size_t positive{};
        std::size_t negative{};
    };
    struct Pending {
        std::size_t positive{NoQuad};
        std::size_t negative{NoQuad};
    };
    std::unordered_map<FaceKey, KeyCounts, FaceKeyHash> keyCounts;
    std::unordered_map<FaceKey, Pending, FaceKeyHash> oldPending;
    keyCounts.reserve(result.quads);
    oldPending.reserve(result.quads);
    auto const near = [tolerance](float a, float b) {
        return std::fabs(a - b) <= tolerance;
    };
    auto const nearInteger = [&](float value, long long& rounded) {
        if (!std::isfinite(value) || std::fabs(value) > 9.0e15F) return false;
        rounded = std::llround(value);
        return near(value, static_cast<float>(rounded));
    };

    for (std::size_t quad = 0; quad < result.quads; ++quad) {
        LiquidShadowQuad face{};
        face.index = quad;
        face.minimum.fill(std::numeric_limits<float>::max());
        face.maximum.fill(std::numeric_limits<float>::lowest());
        bool finite = true;
        for (std::size_t corner = 0; corner < 4U; ++corner) {
            face.vertices[corner] = liquid_face_cull_detail::coordinates(
                positions[quad * 4U + corner]
            );
            for (std::size_t axis = 0; axis < 3U; ++axis) {
                auto const value = face.vertices[corner][axis];
                finite = finite && std::isfinite(value);
                face.minimum[axis] = std::min(face.minimum[axis], value);
                face.maximum[axis] = std::max(face.maximum[axis], value);
            }
        }
        if (finite) {
            for (std::size_t axis = 0; axis < 3U; ++axis) {
                face.extent[axis] = face.maximum[axis] - face.minimum[axis];
            }
            for (int axis = 0; axis < 3; ++axis) {
                if (near(face.minimum[axis], face.maximum[axis])) {
                    face.flatAxis = axis;
                    break;
                }
            }
            if (face.flatAxis < 0) {
                face.classification = LiquidShadowQuadClass::Sloped;
                ++result.slopedFaces;
                ++result.rejectedNonFlat;
            } else {
                face.axisAligned = true;
                auto const first = (face.flatAxis + 1) % 3;
                auto const second = (face.flatAxis + 2) % 3;
                face.plane = face.minimum[face.flatAxis];
                face.firstExtent = face.extent[first];
                face.secondExtent = face.extent[second];
                face.fullUnit = near(face.firstExtent, 1.0F)
                    && near(face.secondExtent, 1.0F);
                if (!face.fullUnit) {
                    face.classification = LiquidShadowQuadClass::PartialAxisAligned;
                    ++result.partialAxisAlignedFaces;
                    ++result.rejectedNonUnit;
                } else {
                    FaceKey key{face.flatAxis, 0, 0, 0};
                    face.integerPlane = nearInteger(face.plane, key.plane);
                    auto const integerFirst = nearInteger(face.minimum[first], key.first);
                    auto const integerSecond = nearInteger(face.minimum[second], key.second);
                    if (!face.integerPlane || !integerFirst || !integerSecond) {
                        face.classification = LiquidShadowQuadClass::NonIntegerFull;
                        ++result.nonIntegerPlaneFaces;
                        ++result.rejectedNonIntegerPlane;
                    } else {
                        auto const& p = face.vertices;
                        std::array<float, 3> const a{
                            p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]
                        };
                        std::array<float, 3> const b{
                            p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]
                        };
                        std::array<float, 3> const normal{
                            a[1] * b[2] - a[2] * b[1],
                            a[2] * b[0] - a[0] * b[2],
                            a[0] * b[1] - a[1] * b[0]
                        };
                        if (std::fabs(normal[face.flatAxis]) <= tolerance) {
                            ++result.unknownFaces;
                        } else {
                            face.classification = LiquidShadowQuadClass::FullUnitInteger;
                            face.winding = normal[face.flatAxis] > 0.0F ? 1 : -1;
                            face.hasKey = true;
                            face.key = key;
                            ++result.fullUnitIntegerFaces;
                            ++result.eligibleUnitQuads;
                            auto& counts = keyCounts[key];
                            auto& pending = oldPending[key];
                            if (face.winding > 0) {
                                ++result.positiveFaces;
                                ++counts.positive;
                            } else {
                                ++result.negativeFaces;
                                ++counts.negative;
                            }
                            auto& opposite = face.winding > 0
                                ? pending.negative : pending.positive;
                            auto& same = face.winding > 0
                                ? pending.positive : pending.negative;
                            if (opposite == NoQuad) {
                                // Exactly the old Praxis pending-face rule:
                                // ignore another same-winding face while one waits.
                                if (same == NoQuad) same = quad;
                            } else {
                                result.oldPairs.push_back({key, opposite, quad});
                                opposite = NoQuad;
                            }
                        }
                    }
                }
            }
        } else {
            ++result.unknownFaces;
        }
        if (quad < MaxLiquidCullDetailQuads) {
            result.detailQuads.push_back(face);
        }
    }
    result.uniqueFaceKeys = keyCounts.size();
    for (auto const& [key, counts] : keyCounts) {
        (void)key;
        result.keysWithPositive += counts.positive != 0U;
        result.keysWithNegative += counts.negative != 0U;
        result.keysWithOppositeWindings += counts.positive != 0U
            && counts.negative != 0U;
        result.duplicatePositiveKeys += counts.positive > 1U;
        result.duplicateNegativeKeys += counts.negative > 1U;
        result.duplicateBothKeys += counts.positive > 1U
            && counts.negative > 1U;
        result.keysBlockedOnlyByDuplicatePolicy += counts.positive != 0U
            && counts.negative != 0U
            && (counts.positive > 1U || counts.negative > 1U);
        result.onePositiveOnlyKeys += counts.positive == 1U && counts.negative == 0U;
        result.oneNegativeOnlyKeys += counts.negative == 1U && counts.positive == 0U;
        result.oneEachKeys += counts.positive == 1U && counts.negative == 1U;
        result.multiplePositiveOneNegativeKeys += counts.positive > 1U
            && counts.negative == 1U;
        result.onePositiveMultipleNegativeKeys += counts.positive == 1U
            && counts.negative > 1U;
        result.multipleBothKeys += counts.positive > 1U && counts.negative > 1U;
    }
    result.oldPraxisPairs = result.oldPairs.size();
    result.oldPraxisRemovedVertices = result.oldPraxisPairs * 8U;
    for (auto const& pair : result.oldPairs) {
        if (result.differences.size() >= MaxLiquidCullDiffKeys) break;
        if (current.valid && current.removeQuads[pair.firstQuad] != 0U
            && current.removeQuads[pair.secondQuad] != 0U) continue;
        auto const& counts = keyCounts.at(pair.key);
        result.differences.push_back({
            pair.key,
            pair.firstQuad,
            pair.secondQuad,
            counts.positive > 1U,
            counts.negative > 1U
        });
    }
    return result;
}

} // namespace lholo::projection::detail
