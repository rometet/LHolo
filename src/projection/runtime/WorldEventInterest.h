#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace lholo::projection::detail {
class WorldEventInterest {
public:
    struct Box {
        std::array<std::int64_t, 3> min{}, max{}; // half-open cell bounds
    };
    WorldEventInterest() = default;
    explicit WorldEventInterest(std::vector<Box> boxes) : mBoxes(std::move(boxes)) {
        if (mBoxes.empty()) return;
        mEnvelope = mBoxes.front();
        for (auto const& box : mBoxes) for (std::size_t axis = 0; axis < 3; ++axis) {
            mEnvelope.min[axis] = (std::min)(mEnvelope.min[axis], box.min[axis]);
            mEnvelope.max[axis] = (std::max)(mEnvelope.max[axis], box.max[axis]);
        }
    }
    bool contains(std::array<int, 3> const& position) const noexcept {
        auto includes = [&](Box const& box) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if (position[axis] < box.min[axis] || position[axis] >= box.max[axis]) return false;
            }
            return true;
        };
        if (mBoxes.empty() || !includes(mEnvelope)) return false;
        return std::any_of(mBoxes.begin(), mBoxes.end(), includes);
    }
    bool intersectsSubChunk(std::array<int, 3> const& coordinates) const noexcept {
        Box chunk;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            chunk.min[axis] = static_cast<std::int64_t>(coordinates[axis]) * 16;
            chunk.max[axis] = chunk.min[axis] + 16;
        }
        auto intersects = [&](Box const& box) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if (chunk.max[axis] <= box.min[axis] || chunk.min[axis] >= box.max[axis]) return false;
            }
            return true;
        };
        if (mBoxes.empty() || !intersects(mEnvelope)) return false;
        return std::any_of(mBoxes.begin(), mBoxes.end(), intersects);
    }
    bool intersectsChunkColumn(int x, int z, int minY, int maxY) const noexcept {
        if (minY > maxY || mBoxes.empty()) return false;
        Box column{{static_cast<std::int64_t>(x) * 16, minY, static_cast<std::int64_t>(z) * 16},
                   {static_cast<std::int64_t>(x) * 16 + 16, static_cast<std::int64_t>(maxY) + 1,
                    static_cast<std::int64_t>(z) * 16 + 16}};
        auto intersects = [&](Box const& box) {
            for (std::size_t axis = 0; axis < 3; ++axis)
                if (column.max[axis] <= box.min[axis] || column.min[axis] >= box.max[axis]) return false;
            return true;
        };
        return intersects(mEnvelope) && std::any_of(mBoxes.begin(), mBoxes.end(), intersects);
    }
private:
    std::vector<Box> mBoxes;
    Box mEnvelope{};
};
} // namespace lholo::projection::detail
