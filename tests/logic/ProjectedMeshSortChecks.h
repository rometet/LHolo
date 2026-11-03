#pragma once
#include "projection/core/ProjectionCachedDistanceSort.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lholo::tests {
template <class Check>
void runProjectedMeshSortChecks(Check check) {
    struct Draw {
        std::size_t bucket{}, section{};
        bool operator==(Draw const&) const = default;
    };
    // Keep the original comparator as the order oracle; multiple buckets
    // can share a section with exactly equal camera distance.
    for (std::uint32_t iteration = 0; iteration < 60; ++iteration) {
        constexpr std::size_t kSections = 393;
        std::vector<Draw> original;
        original.reserve(kSections * 4);
        std::uint32_t state = iteration + 9;
        auto next = [&]() {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        };
        for (std::size_t section = 0; section < kSections; ++section) {
            for (std::size_t bucket = 0; bucket < 4; ++bucket) {
                if (next() % 3) original.push_back({bucket, section});
            }
        }
        std::vector<float> distances(kSections);
        for (std::size_t section = 0; section < kSections; ++section) {
            // Some sections tie; orientation changes vary ordering.
            auto const x = static_cast<int>((section * 17 + iteration * 23) % 173) - 86;
            distances[section] = static_cast<float>(x * x);
        }
        auto expected = original;
        std::sort(expected.begin(), expected.end(), [&](Draw const& a, Draw const& b) {
            auto const da = distances[a.section], db = distances[b.section];
            if (da != db) return da > db;
            if (a.section != b.section) return a.section < b.section;
            return a.bucket < b.bucket;
        });
        std::vector<std::uint8_t> accessed(kSections);
        std::size_t calls{};
        projection::detail::sortProjectionDrawsByCachedDistance(
            original, kSections, [&](std::size_t section) {
                ++calls; ++accessed[section];
                return distances[section];
            });
        check(original == expected);
        for (auto const count : accessed) check(count <= 1);
        check(calls <= kSections);
    }
    std::vector<Draw> empty;
    std::size_t calls{};
    projection::detail::sortProjectionDrawsByCachedDistance(empty, 0, [&](std::size_t) { ++calls; return 0.0f; });
    check(empty.empty() && calls == 0);
    std::vector<Draw> single{{2, 17}};
    projection::detail::sortProjectionDrawsByCachedDistance(single, 32, [&](std::size_t) { ++calls; return 0.0f; });
    check(single.size() == 1 && single.front().section == 17 && calls == 0);
}
} // namespace lholo::tests
