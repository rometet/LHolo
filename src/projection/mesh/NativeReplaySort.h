// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026 MarmieQi
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "projection/mesh/TransparentQuadSort.h"
#include <chrono>
namespace lholo::projection::detail {
// Only LHolo-owned permutation indices persist. Published canonical streams
// stay immutable; newly built/merged/culled data constructs a fresh cache.
struct NativeReplaySortCache {
    TransparentSortKey key{};
    bool keyValid{};
    bool unsupported{};
    std::vector<std::size_t> order;
};
struct NativeReplaySortBudget {
    std::size_t attempts{};
    std::chrono::steady_clock::time_point started{std::chrono::steady_clock::now()};
};
inline std::span<std::size_t const> nativeReplayQuadOrder(
    NativeReplaySortCache& cache, std::span<glm::vec3 const> positions,
    glm::vec3 camera, NativeReplaySortBudget& budget
) {
    auto key=transparentSortKey(camera);
    if (!key || cache.unsupported) return {};
    if (cache.keyValid && cache.key==*key) return cache.order;
    if (budget.attempts == 0) budget.started = std::chrono::steady_clock::now();
    // Share the existing sort cadence/count/time limit across all replay
    // submissions, including per-section fallback. An old valid order is safe
    // while this owner waits for budget; an initial miss uses native order.
    if (budget.attempts>=4 ||
        std::chrono::steady_clock::now()-budget.started>=std::chrono::milliseconds(1))
        return cache.order;
    ++budget.attempts;
    auto order=transparentQuadOrder(positions,camera);
    if (!order) {cache.unsupported=true;return {};}
    cache.order=std::move(*order);cache.key=*key;cache.keyValid=true;
    return cache.order;
}
template<class Value>
bool reorderReplayQuadMetadata(std::vector<Value>& field,std::span<std::size_t const> order) {
    if (field.empty()) return true;
    if (field.size()!=order.size()) return false;
    std::vector<Value> sorted;sorted.reserve(field.size());
    for (auto const index:order) {
        if (index>=field.size()) return false;
        sorted.push_back(field[index]);
    }
    field=std::move(sorted);return true;
}
} // namespace lholo::projection::detail

