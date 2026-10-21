// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026 MarmieQi
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace lholo::projection::detail {
// Task-owned component bytes for target cells only. No borrowed cache pointer
// and no structure-sized per-job copy; correction neighbors retain their owner.
class SectionMissingLayerSnapshot {
public:
    void capture(std::vector<std::size_t> indices, std::vector<std::uint8_t> const& source) {
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        if (!indices.empty() && indices.back() >= source.size()) {
            throw std::out_of_range("section missing-layer snapshot block index");
        }
        std::vector<std::uint8_t> values;
        values.reserve(indices.size());
        for (auto index : indices) values.push_back(source[index]);
        mIndices = std::move(indices);
        mValues = std::move(values);
    }
    std::uint8_t const* find(std::size_t index) const noexcept {
        auto const found = std::lower_bound(mIndices.begin(), mIndices.end(), index);
        return found != mIndices.end() && *found == index ? &mValues[found - mIndices.begin()] : nullptr;
    }
    std::size_t size() const noexcept { return mIndices.size(); }
    std::size_t bytes() const noexcept { return mIndices.size() * sizeof(std::size_t) + mValues.size(); }
private:
    std::vector<std::size_t> mIndices;
    std::vector<std::uint8_t> mValues;
};
} // namespace lholo::projection::detail
