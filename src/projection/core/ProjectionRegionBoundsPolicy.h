#pragma once

#include <cstddef>

namespace lholo::projection::detail {

// The blue outer placement always remains visible when the master setting
// is enabled. Yellow subregion edges coinciding with it are masked at mesh
// construction time to avoid coplanar Z-fighting.
inline constexpr std::size_t kMaximumNativeRegionBounds = 128;

inline bool shouldDrawNativeRegionBounds(bool masterEnabled,
    bool regionEnabled, std::size_t sourceRegionCount) noexcept {
    return masterEnabled && regionEnabled
        && sourceRegionCount > 1
        && sourceRegionCount <= kMaximumNativeRegionBounds;
}

inline bool shouldDrawOverallPlacementBounds(bool masterEnabled,
    bool regionEnabled, std::size_t sourceRegionCount) noexcept {
    (void)regionEnabled;
    (void)sourceRegionCount;
    return masterEnabled; // The enclosing blue boundary always takes priority.
}

// An already-outlined subregion needs no duplicate selected-region draw.
// When native-region outlines are disabled, a selected subregion can still
// use a yellow highlight, with blue outer-coincident edges clipped.
inline bool shouldDrawSelectedRegionHighlight(bool masterEnabled,
    bool regionEnabled, std::size_t sourceRegionCount) noexcept {
    return masterEnabled
        && !shouldDrawNativeRegionBounds(
            masterEnabled, regionEnabled, sourceRegionCount);
}

} // namespace lholo::projection::detail
