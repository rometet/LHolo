#pragma once

#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <span>

namespace lholo::projection::detail {

// Re-evaluate camera distance on each admission. Incremental corrections have
// priority over initial convergence; equal distances preserve section order.
template <class Section, class Distance>
[[nodiscard]] std::optional<std::size_t> selectDirtySection(
    std::span<Section const> sections, Distance&& distanceSquared
) {
    std::optional<std::size_t> selected;
    bool selectedIncremental{};
    float selectedDistance = (std::numeric_limits<float>::max)();
    for (std::size_t index = 0; index < sections.size(); ++index) {
        auto const& section = sections[index];
        if (!section.dirty || section.buildInFlight) continue;
        auto const distance = std::invoke(distanceSquared, section);
        if (!selected || (section.incrementalDirty && !selectedIncremental)
            || (section.incrementalDirty == selectedIncremental && distance < selectedDistance)) {
            selected = index;
            selectedIncremental = section.incrementalDirty;
            selectedDistance = distance;
        }
    }
    return selected;
}

} // namespace lholo::projection::detail
