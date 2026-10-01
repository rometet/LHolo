#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <limits>

namespace lholo::projection::detail {

struct SectionGrid {
    std::size_t x{}, y{}, z{};
    [[nodiscard]] std::optional<std::size_t> denseCount(std::uint64_t limit) const {
        if (!x || !y || !z) return std::nullopt;
        std::uint64_t count = 1;
        for (auto const extent : {x, y, z}) {
            if (extent > limit / count) return std::nullopt;
            count *= extent;
        }
        return static_cast<std::size_t>(count);
    }
};

inline std::optional<SectionGrid> makeSectionGrid(int x, int y, int z) {
    if (x <= 0 || y <= 0 || z <= 0) return std::nullopt;
    auto const sections = [](int extent) {
        return static_cast<std::size_t>((static_cast<std::int64_t>(extent) + 15) / 16);
    };
    return SectionGrid{sections(x), sections(y), sections(z)};
}

struct ProjectionRangeBox {
    std::array<std::int64_t, 3> min{}, max{};
};

inline std::optional<std::array<int, 3>> checkedRelativeBlockCell(
    std::array<int, 3> world, std::array<int, 3> anchor, std::array<int, 3> offset
) noexcept {
    std::array<int, 3> relative{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        auto const value = static_cast<std::int64_t>(world[axis]) - anchor[axis] - offset[axis];
        if (value < (std::numeric_limits<int>::min)() || value > (std::numeric_limits<int>::max)()) {
            return std::nullopt;
        }
        relative[axis] = static_cast<int>(value);
    }
    return relative;
}

inline std::optional<ProjectionRangeBox> checkedSubChunkBlockBounds(std::array<int, 3> section) noexcept {
    ProjectionRangeBox bounds;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        bounds.min[axis] = static_cast<std::int64_t>(section[axis]) * 16;
        bounds.max[axis] = bounds.min[axis] + 16; // Exclusive, may equal INT_MAX+1.
        if (bounds.min[axis] < (std::numeric_limits<int>::min)()
            || bounds.max[axis] - 1 > (std::numeric_limits<int>::max)()) return std::nullopt;
    }
    return bounds;
}

inline std::optional<std::array<int, 3>> checkedBlockCell(std::array<float, 3> position, int padding = 0) {
    if (padding < 0) return std::nullopt;
    std::array<int, 3> cell{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(position[axis])) return std::nullopt;
        auto const value = std::floor(static_cast<double>(position[axis]));
        if (value < static_cast<double>((std::numeric_limits<int>::min)()) + padding
            || value > static_cast<double>((std::numeric_limits<int>::max)()) - padding) return std::nullopt;
        cell[axis] = static_cast<int>(value);
    }
    return cell;
}

inline std::optional<std::array<int, 3>> checkedVoxelRayOrigin(
    std::array<float, 3> origin, std::array<float, 3> direction, float distance
) {
    if (!std::isfinite(distance) || distance < 0) return std::nullopt;
    bool nonzero{};
    for (auto const value : direction) {
        if (!std::isfinite(value)) return std::nullopt;
        nonzero = nonzero || value != 0;
    }
    // Existing DDA advances at most 512 cells and may query one support neighbor.
    return nonzero ? checkedBlockCell(origin, 513) : std::nullopt;
}

// Keep the full transformed volume and a section of neighbor padding inside
// the engine's int coordinate domain before any world-coordinate additions.
// Chunk-view preparation uses +/-2; block models also query their neighbors.
inline std::optional<std::array<int, 3>> checkedProjectionOrigin(
    std::array<int, 3> anchor,
    std::array<int, 3> offset,
    std::array<int, 3> dimensions,
    int rotation
) {
    if (rotation & 1) std::swap(dimensions[0], dimensions[2]);
    std::array<int, 3> origin{};
    constexpr std::int64_t padding = 16;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (dimensions[axis] <= 0) return std::nullopt;
        auto const begin = static_cast<std::int64_t>(anchor[axis]) + offset[axis];
        auto const end = begin + dimensions[axis] - 1;
        if (begin - padding < (std::numeric_limits<int>::min)()
            || end + padding > (std::numeric_limits<int>::max)()) return std::nullopt;
        origin[axis] = static_cast<int>(begin);
    }
    return origin;
}

inline std::optional<ProjectionRangeBox> projectionRangeBox(
    std::array<float, 3> center, float radius
) {
    if (!std::isfinite(radius) || radius < 0 || radius > 64) return std::nullopt;
    ProjectionRangeBox result;
    auto const r = static_cast<std::int64_t>(std::ceil(radius));
    constexpr auto lower = (std::numeric_limits<int>::min)();
    constexpr auto upper = (std::numeric_limits<int>::max)();
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(center[axis])) return std::nullopt;
        auto const floored = std::floor(static_cast<double>(center[axis]));
        if (floored < lower || floored > upper) return std::nullopt;
        auto const cell = static_cast<std::int64_t>(floored);
        result.min[axis] = (std::max)(cell - r, static_cast<std::int64_t>(lower));
        result.max[axis] = (std::min)(cell + r, static_cast<std::int64_t>(upper));
    }
    return result;
}

} // namespace lholo::projection::detail
