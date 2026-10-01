// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <utility>
#include <vector>
#include <glm/vec3.hpp>

namespace lholo::projection::detail {

using TransparentSortKey = std::array<std::int64_t, 3>;

inline std::optional<TransparentSortKey> transparentSortKey(glm::vec3 camera) noexcept {
    TransparentSortKey key{};
    // int64 max rounds upward in double. Use the exclusive upper bound so a
    // float camera exactly at 2^61 cannot reach an undefined integer cast.
    constexpr double lower = -9223372036854775808.0;
    constexpr double upper =  9223372036854775808.0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        auto const value = static_cast<double>(camera[axis]);
        if (!std::isfinite(value)) return std::nullopt;
        auto const scaled = std::floor(value * 4.0);
        if (scaled < lower || scaled >= upper) return std::nullopt;
        key[axis] = static_cast<std::int64_t>(scaled);
    }
    return key;
}

namespace transparent_sort_detail {
template<class PositionAt>
inline std::optional<std::vector<std::size_t>> order(
    std::size_t elementCount, std::size_t corners, glm::vec3 camera, PositionAt positionAt
) {
    if ((corners != 3U && corners != 4U) || !elementCount || elementCount % corners
        || !std::isfinite(camera.x) || !std::isfinite(camera.y) || !std::isfinite(camera.z)) return std::nullopt;
    auto const primitiveCount = elementCount / corners;
    std::vector<double> distances(primitiveCount);
    std::vector<std::array<double, 3>> centroids(primitiveCount);
    std::vector<std::size_t> indices(primitiveCount);
    std::iota(indices.begin(), indices.end(), 0U);
    for (std::size_t primitive = 0; primitive < primitiveCount; ++primitive) {
        auto& center = centroids[primitive];
        for (std::size_t corner = 0; corner < corners; ++corner) {
            auto const position = positionAt(primitive * corners + corner);
            if (!position) return std::nullopt;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                auto const value = static_cast<double>((*position)[axis]);
                if (!std::isfinite(value)) return std::nullopt;
                center[axis] += value / static_cast<double>(corners);
            }
        }
        // Double prevents centroid accumulation and squared float-distance
        // overflow from violating the sorting comparator's ordering contract.
        for (std::size_t axis = 0; axis < 3; ++axis) {
            auto const delta = center[axis] - static_cast<double>(camera[axis]);
            distances[primitive] += delta * delta;
        }
    }
    std::sort(indices.begin(), indices.end(), [&](std::size_t lhs, std::size_t rhs) {
        if (distances[lhs] != distances[rhs]) return distances[lhs] > distances[rhs];
        if (centroids[lhs] != centroids[rhs]) return centroids[lhs] < centroids[rhs];
        return lhs < rhs;
    });
    return indices;
}
} // namespace transparent_sort_detail

inline std::optional<std::vector<std::size_t>> transparentPrimitiveOrder(
    std::span<glm::vec3 const> positions, std::size_t corners, glm::vec3 camera
) {
    return transparent_sort_detail::order(positions.size(), corners, camera,
        [&](std::size_t i) { return &positions[i]; });
}

inline std::optional<std::vector<std::size_t>> transparentIndexedPrimitiveOrder(
    std::span<glm::vec3 const> positions, std::span<unsigned int const> indices,
    std::size_t corners, glm::vec3 camera
) {
    return transparent_sort_detail::order(indices.size(), corners, camera,
        [&](std::size_t i) -> glm::vec3 const* {
            return indices[i] < positions.size() ? &positions[indices[i]] : nullptr;
        });
}

inline std::optional<std::vector<std::size_t>> transparentQuadOrder(
    std::span<glm::vec3 const> positions, glm::vec3 camera
) { return transparentPrimitiveOrder(positions, 4U, camera); }

template <class T>
bool reorderPrimitiveField(std::vector<T>& field, std::span<std::size_t const> order, std::size_t corners) {
    if ((corners != 3U && corners != 4U) || order.size() > (std::numeric_limits<std::size_t>::max)() / corners)
        return false;
    // Validate the complete permutation before moving a single attribute.
    // An invalid order must leave even a move-only field unchanged.
    std::vector<bool> seen(order.size());
    for (auto const primitive : order) {
        if (primitive >= order.size() || seen[primitive]) return false;
        seen[primitive] = true;
    }
    if (field.empty()) return true;
    auto const expected = order.size() * corners;
    if (field.size() != expected) return false;
    std::vector<T> sorted;
    sorted.reserve(expected);
    for (auto const primitive : order) {
        auto const first = primitive * corners;
        for (std::size_t corner = 0; corner < corners; ++corner) {
            sorted.push_back(std::move(field[first + corner]));
        }
    }
    field = std::move(sorted);
    return true;
}

template <class T>
bool reorderQuadVertexField(std::vector<T>& field, std::span<std::size_t const> order) {
    return reorderPrimitiveField(field, order, 4U);
}

inline bool transparentQuadOrderChanged(std::span<std::size_t const> order) noexcept {
    for (std::size_t index = 0; index < order.size(); ++index) {
        if (order[index] != index) return true;
    }
    return false;
}

} // namespace lholo::projection::detail
