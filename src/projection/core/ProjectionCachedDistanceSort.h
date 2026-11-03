#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

namespace lholo::projection::detail {
// Preserve the exact legacy back-to-front comparator and its section/bucket
// tie breakers while calculating squared camera distance only once per source
// section. Dense large schematics often have three or four material buckets
// for each section, so repeated comparison-time distance math is wasteful.
// The cache is invocation-local: no stale camera, world or GPU references.
template <class Draw, class ReadDistance>
void sortProjectionDrawsByCachedDistance(
    std::vector<Draw>& draws, std::size_t sectionCount, ReadDistance&& distanceForSection
) {
    if (draws.size() < 2) return;
    std::vector<std::optional<float>> squared(sectionCount);
    for (auto const& draw : draws) {
        if (draw.section < squared.size() && !squared[draw.section].has_value()) {
            squared[draw.section] = distanceForSection(draw.section);
        }
    }
    auto const distance = [&](std::size_t section) {
        return section < squared.size() && squared[section].has_value()
            ? *squared[section] : distanceForSection(section);
    };
    std::sort(draws.begin(), draws.end(), [&](Draw const& lhs, Draw const& rhs) {
        auto const leftDistance = distance(lhs.section);
        auto const rightDistance = distance(rhs.section);
        if (leftDistance != rightDistance) return leftDistance > rightDistance;
        if (lhs.section != rhs.section) return lhs.section < rhs.section;
        return lhs.bucket < rhs.bucket;
    });
}
} // namespace lholo::projection::detail
