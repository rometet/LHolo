#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <vector>

namespace lholo::projection::detail {
// Value-only chunk availability facts. Expand tall columns incrementally into
// the existing subchunk refresh path; never retain a native chunk/source.
class ChunkAvailabilityQueue {
public:
    static int subChunkY(int blockY) noexcept {
        auto const wide = static_cast<std::int64_t>(blockY);
        return static_cast<int>(wide >= 0 ? wide / 16 : (wide - 15) / 16);
    }
    void push(int x, int z, int minBlockY, int maxBlockY) {
        if (minBlockY > maxBlockY) return;
        Span const span{subChunkY(minBlockY), subChunkY(maxBlockY)};
        auto [it, added] = mColumns.try_emplace(std::array{x, z}, span);
        if (!added) {
            it->second.next = (std::min)(it->second.next, span.next);
            it->second.last = (std::max)(it->second.last, span.last);
        }
    }
    template<class Interested>
    std::vector<std::array<int, 3>> take(std::size_t limit, std::size_t budget, Interested interested) {
        std::vector<std::array<int, 3>> result;
        result.reserve((std::min)(limit, budget));
        while (!mColumns.empty() && budget && result.size() < limit) {
            --budget;
            auto it = mColumns.begin();
            auto const cell = std::array{it->first[0], static_cast<int>(it->second.next++), it->first[1]};
            if (it->second.next > it->second.last) mColumns.erase(it);
            if (interested(cell)) result.push_back(cell);
        }
        return result;
    }
    void clear() noexcept { mColumns.clear(); }
    bool empty() const noexcept { return mColumns.empty(); }
private:
    struct Span { std::int64_t next{}, last{}; };
    std::map<std::array<int, 2>, Span> mColumns;
};
} // namespace lholo::projection::detail
