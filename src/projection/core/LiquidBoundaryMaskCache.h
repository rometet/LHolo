// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#pragma once

#include "projection/core/ProjectionLiquidFaceCull.h"

#include <optional>

namespace lholo::projection::detail {

struct LiquidSectionQuadCount {
    std::size_t section{};
    std::size_t quads{};
};

// The render owner invalidates this cache whenever a native section stream
// changes. Camera order alone permutes the existing quad decisions, avoiding
// another geometry hash pass. No engine object or vertex stream is retained.
class LiquidBoundaryMaskCache {
public:
    void clear() noexcept {
        mRanges.clear();
        mMask = {};
    }

    bool remember(std::span<LiquidSectionQuadCount const> sections,
                  NativeLiquidFaceCullMask const& mask) {
        if (!mask.valid || sections.empty()) return false;
        std::vector<Range> ranges;
        ranges.reserve(sections.size());
        std::size_t total{};
        for (auto const& section : sections) {
            if (section.quads > mask.removeQuads.size() - total) return false;
            ranges.push_back({section.section, total, section.quads});
            total += section.quads;
        }
        if (total != mask.removeQuads.size()) return false;
        std::size_t removed{};
        for (auto const value : mask.removeQuads) {
            if (value > 1U) return false;
            removed += value;
        }
        if (removed % 2U != 0U || removed / 2U != mask.facePairs) return false;
        std::sort(ranges.begin(), ranges.end(), [](Range const& a, Range const& b) {
            return a.section < b.section;
        });
        for (std::size_t i = 1; i < ranges.size(); ++i) {
            if (ranges[i - 1].section == ranges[i].section) return false;
        }
        // Build everything before publication so allocation failure preserves
        // the preceding cache. The caller leaves its aggregate dirty to retry.
        auto nextMask = mask;
        mRanges.swap(ranges);
        mMask = std::move(nextMask);
        return true;
    }

    [[nodiscard]] std::optional<NativeLiquidFaceCullMask> forOrder(
        std::span<LiquidSectionQuadCount const> sections
    ) const {
        if (!mMask.valid || sections.size() != mRanges.size()) return {};
        NativeLiquidFaceCullMask result{true, {}, mMask.facePairs};
        result.removeQuads.reserve(mMask.removeQuads.size());
        std::vector<std::uint8_t> seen(mRanges.size());
        for (auto const& section : sections) {
            auto const found = std::lower_bound(mRanges.begin(), mRanges.end(), section.section,
                [](Range const& range, std::size_t id) { return range.section < id; });
            if (found == mRanges.end() || found->section != section.section
                || found->count != section.quads) return {};
            auto const index = static_cast<std::size_t>(found - mRanges.begin());
            if (seen[index] != 0U) return {};
            seen[index] = 1U;
            auto const first = mMask.removeQuads.begin() + found->offset;
            result.removeQuads.insert(result.removeQuads.end(), first, first + found->count);
        }
        return result;
    }

private:
    struct Range { std::size_t section, offset, count; };
    std::vector<Range> mRanges;
    NativeLiquidFaceCullMask mMask;
};

// Compact typed streams in place, preserving the native order and each value.
// Empty optional streams are allowed; inconsistent layouts remain untouched.
template <class Value>
bool compactLiquidQuadField(std::vector<Value>& field,
                            std::span<std::uint8_t const> removeQuads,
                            std::size_t valuesPerQuad) {
    if (field.empty()) return true;
    if (valuesPerQuad == 0U || field.size() % valuesPerQuad != 0U
        || field.size() / valuesPerQuad != removeQuads.size()) return false;
    std::size_t write{};
    for (std::size_t quad = 0; quad < removeQuads.size(); ++quad) {
        if (removeQuads[quad] != 0U) continue;
        auto const read = quad * valuesPerQuad;
        for (std::size_t i = 0; i < valuesPerQuad; ++i) {
            if (write != read + i) field[write] = std::move(field[read + i]);
            ++write;
        }
    }
    field.resize(write);
    return true;
}

} // namespace lholo::projection::detail
