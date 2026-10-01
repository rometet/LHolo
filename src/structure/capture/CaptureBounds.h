#pragma once

#include "structure/capture/StructureCapture.h"
#include <array>
#include <algorithm>
#include <limits>

namespace lholo::structure::capture::detail {

inline std::array<std::uint64_t, 3> captureSize(Point first, Point second) noexcept {
    auto extent = [](int a, int b) {
        auto const delta = static_cast<std::int64_t>(a) - b;
        return static_cast<std::uint64_t>(delta < 0 ? -delta : delta) + 1;
    };
    return {extent(first.x, second.x), extent(first.y, second.y), extent(first.z, second.z)};
}

inline std::optional<std::uint64_t> captureVolume(Point first, Point second) noexcept {
    std::uint64_t volume = 1;
    for (auto size : captureSize(first, second)) {
        if (size > (std::numeric_limits<std::uint64_t>::max)() / volume) return std::nullopt;
        volume *= size;
    }
    return volume;
}

inline bool captureBoundsSupported(Point first, Point second) noexcept {
    // Native bounds use signed int extents and inclusive neighbors/loop ends.
    // Each mcstructure cell needs two int32 layer indices. Do not request a
    // native allocation whose indices alone exceed our existing 512 MiB input
    // limit (the output also requires palette, names and other NBT metadata).
    constexpr std::uint64_t maximumCells = (512ULL * 1024 * 1024 - 1) / 8;
    for (auto size : captureSize(first, second)) {
        if (size > static_cast<std::uint64_t>((std::numeric_limits<int>::max)())) return false;
    }
    for (auto coordinate : {first.x, first.y, first.z, second.x, second.y, second.z}) {
        if (coordinate == (std::numeric_limits<int>::min)()
            || coordinate == (std::numeric_limits<int>::max)()) return false;
    }
    auto const volume = captureVolume(first, second);
    return volume && *volume <= maximumCells;
}
} // namespace lholo::structure::capture::detail
