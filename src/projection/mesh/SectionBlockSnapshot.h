#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
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
        if (!indices.empty() && (indices.back() >= corrections.size() || indices.back() >= actors.size())) {
            throw std::out_of_range("section snapshot block index");
        }
        std::size_t rangeCount = indices.empty() ? 0 : 1;
        for (std::size_t i = 1; i < indices.size(); ++i) {
            if (indices[i] - indices[i - 1] != 1) ++rangeCount;
        }
        // Section-layout indices contain contiguous runs. Search one descriptor
        // per run and address the immutable byte pair directly within it. Keep
        // the original entries for sparse indices; never expand a sparse gap.
        // This conservative admission also bounds storage below Entry bytes.
        if (rangeCount && rangeCount <= indices.size() / 4) {
            std::vector<Range> ranges;
            std::vector<Value> values;
            ranges.reserve(rangeCount);
            values.reserve(indices.size());
            for (auto const index : indices) {
                if (ranges.empty() || index - ranges.back().last != 1) {
                    ranges.push_back({index, index, values.size()});
                } else ranges.back().last = index;
                values.push_back({static_cast<std::uint8_t>(corrections[index]), actors[index]});
            }
            mRanges = std::move(ranges);
            mValues = std::move(values);
            std::vector<Entry>{}.swap(mEntries);
            return;
        }
        std::vector<Range>{}.swap(mRanges);
        std::vector<Value>{}.swap(mValues);
        mEntries.clear();
        mEntries.reserve(indices.size());
        for (auto const index : indices) {
            mEntries.push_back({index, {static_cast<std::uint8_t>(corrections[index]), actors[index]}});
        }
    }

    Value const* find(std::size_t index) const noexcept {
        if (!mRanges.empty()) {
            auto const found = std::lower_bound(mRanges.begin(), mRanges.end(), index,
                [](Range const& range, std::size_t key) { return range.last < key; });
            return found != mRanges.end() && index >= found->first
                ? &mValues[found->offset + (index - found->first)] : nullptr;
        }
        auto const found = std::lower_bound(mEntries.begin(), mEntries.end(), index,
            [](Entry const& entry, std::size_t key) { return entry.index < key; });
        return found != mEntries.end() && found->index == index ? &found->value : nullptr;
    }
    std::size_t bytes() const noexcept {
        return mEntries.size() * sizeof(Entry) + mRanges.size() * sizeof(Range) + mValues.size() * sizeof(Value);
    }
    std::size_t size() const noexcept { return mEntries.size() + mValues.size(); }
private:
    struct Range { std::size_t first{}, last{}, offset{}; };
    std::vector<Entry> mEntries;
    std::vector<Range> mRanges;
    std::vector<Value> mValues;
};

} // namespace lholo::projection::detail
