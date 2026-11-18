// Task-local 18^3 correction priority lookup for a projected section.
// The caller supplies already-transformed projection-local cells.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace lholo::projection::detail {
class LocalCorrectionPriorityGrid {
public:
    bool reset(int minX, int minY, int minZ, int maxX, int maxY, int maxZ) {
        auto const dx = static_cast<std::int64_t>(maxX) - minX + 1;
        auto const dy = static_cast<std::int64_t>(maxY) - minY + 1;
        auto const dz = static_cast<std::int64_t>(maxZ) - minZ + 1;
        // A section occupies at most 16^3 cells, plus one face-neighbor
        // on each side. Refuse any unexpectedly large/sparse input and
        // let the caller use its original virtual-world lookup instead.
        if (dx <= 0 || dy <= 0 || dz <= 0
            || dx > 18 || dy > 18 || dz > 18) {
            mCells.clear();
            mX = mY = mZ = 0;
            return false;
        }
        mOriginX = minX; mOriginY = minY; mOriginZ = minZ;
        mY = static_cast<std::size_t>(dy);
        mZ = static_cast<std::size_t>(dz);
        mX = static_cast<std::size_t>(dx);
        mCells.assign(mX * mY * mZ, 0);
        return true;
    }
    void add(int x, int y, int z, std::uint8_t priority) noexcept {
        if (!priority) return;
        if (auto const cell = index(x, y, z)) {
            mCells[*cell] = (std::max)(mCells[*cell], priority);
        }
    }
    std::uint8_t get(int x, int y, int z) const noexcept {
        auto const cell = index(x, y, z);
        return cell ? mCells[*cell] : 0;
    }
    std::size_t bytes() const noexcept { return mCells.size(); }
private:
    // The exact same bounds check also handles negative local coordinates.
    std::optional<std::size_t> index(int x, int y, int z) const noexcept {
        auto const dx = static_cast<std::int64_t>(x) - mOriginX;
        auto const dy = static_cast<std::int64_t>(y) - mOriginY;
        auto const dz = static_cast<std::int64_t>(z) - mOriginZ;
        if (dx < 0 || dy < 0 || dz < 0
            || static_cast<std::uint64_t>(dx) >= mX
            || static_cast<std::uint64_t>(dy) >= mY
            || static_cast<std::uint64_t>(dz) >= mZ) return std::nullopt;
        return (static_cast<std::size_t>(dx) * mY + static_cast<std::size_t>(dy)) * mZ
            + static_cast<std::size_t>(dz);
    }
    int mOriginX{}, mOriginY{}, mOriginZ{};
    std::size_t mX{}, mY{}, mZ{};
    std::vector<std::uint8_t> mCells;
};
} // namespace lholo::projection::detail
