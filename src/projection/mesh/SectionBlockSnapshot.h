#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace lholo::projection::detail {

// Task-local mutable bytes for a section and its six direct neighbors. Native
// virtual-world maps remain shared/immutable, including all liquid neighbors.
class SectionBlockSnapshot {
public:
    struct Value { std::uint8_t correction{}, actorRenderer{}; };
    struct Entry { std::size_t index{}; Value value; };

    template <class Correction, class Actors>
    void capture(std::vector<std::size_t> indices, Correction const& corrections, Actors const& actors) {
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        mEntries.clear();
        mEntries.reserve(indices.size());
        for (auto const index : indices) {
            if (index >= corrections.size() || index >= actors.size()) {
                throw std::out_of_range("section snapshot block index");
            }
            mEntries.push_back({index, {static_cast<std::uint8_t>(corrections[index]), actors[index]}});
        }
    }

    Value const* find(std::size_t index) const noexcept {
        auto const found = std::lower_bound(mEntries.begin(), mEntries.end(), index,
            [](Entry const& entry, std::size_t key) { return entry.index < key; });
        return found != mEntries.end() && found->index == index ? &found->value : nullptr;
    }
    std::size_t bytes() const noexcept { return mEntries.size() * sizeof(Entry); }
    std::size_t size() const noexcept { return mEntries.size(); }
private:
    std::vector<Entry> mEntries;
};

} // namespace lholo::projection::detail
