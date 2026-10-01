// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/world/ProjectionQueries.h"

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/core/ProjectionState.h"
#include "projection/core/ProjectionCoordinateBounds.h"

#include <algorithm>
#include <cmath>
#include <tuple>

#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/material/Material.h"

namespace lholo::projection::detail {

ProjectionQuery queryProjectionCell(
    ProjectionState const& state,
    BlockPos const&        worldPosition
) {
    if (!state.expectedWorldBlockIndices || !state.expectedWorldBlocks) {
        return {nullptr, false};
    }
    auto const key = std::tuple{worldPosition.x, worldPosition.y, worldPosition.z};
    auto const foundIndex = state.expectedWorldBlockIndices->find(key);
    if (foundIndex == state.expectedWorldBlockIndices->end()
        || foundIndex->second >= state.correctionStates.size()) {
        return {nullptr, false};
    }
    auto const foundBlock = state.expectedWorldBlocks->find(key);
    Block const* block = foundBlock == state.expectedWorldBlocks->end()
        ? nullptr
        : foundBlock->second;
    // Liquids have no normal block item, so they are never a valid place target.
    if (block && block->getBlockType().mMaterial.mLiquid) block = nullptr;
    bool const missing = state.correctionStates[foundIndex->second] == CorrectionState::Missing;
    return {block, missing};
}

std::vector<RangeCandidate> queryMissingProjectionCells(
    ProjectionState const& state,
    Vec3 const&            center,
    float                  radius
) {
    std::vector<RangeCandidate> result;
    auto const bounds = projectionRangeBox({center.x, center.y, center.z}, radius);
    if (!state.expectedWorldBlockIndices || !state.expectedWorldBlocks || !bounds) {
        return result;
    }
    // Only visit cells in the axis-aligned box around the center: with a small
    // radius this is far cheaper than walking the whole virtual-world map.
    double const r2 = static_cast<double>(radius) * radius;
    // The loop counters can advance past INT_MAX without signed overflow;
    // every actual cell remains within the checked int-coordinate bounds.
    for (auto y = bounds->min[1]; y <= bounds->max[1]; ++y) {
        for (auto z = bounds->min[2]; z <= bounds->max[2]; ++z) {
            for (auto x = bounds->min[0]; x <= bounds->max[0]; ++x) {
                auto const key = std::tuple{static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)};
                auto const foundIndex = state.expectedWorldBlockIndices->find(key);
                if (foundIndex == state.expectedWorldBlockIndices->end()
                    || foundIndex->second >= state.correctionStates.size()) {
                    continue;
                }
                if (state.correctionStates[foundIndex->second] != CorrectionState::Missing) continue;
                double const dx = static_cast<double>(x) + 0.5 - center.x;
                double const dy = static_cast<double>(y) + 0.5 - center.y;
                double const dz = static_cast<double>(z) + 0.5 - center.z;
                if (dx * dx + dy * dy + dz * dz > r2) continue;
                auto const foundBlock = state.expectedWorldBlocks->find(key);
                Block const* block = foundBlock == state.expectedWorldBlocks->end()
                    ? nullptr
                    : foundBlock->second;
                // Liquids have no normal block item, so they are never a valid place target.
                if (block && block->getBlockType().mMaterial.mLiquid) block = nullptr;
                if (!block) continue;
                result.push_back({static_cast<int>(x), static_cast<int>(y), static_cast<int>(z), block});
            }
        }
    }
    std::sort(result.begin(), result.end(), [&center](RangeCandidate const& a, RangeCandidate const& b) {
        auto const distSq = [&center](RangeCandidate const& candidate) {
            double const dx = static_cast<double>(candidate.x) + 0.5 - center.x;
            double const dy = static_cast<double>(candidate.y) + 0.5 - center.y;
            double const dz = static_cast<double>(candidate.z) + 0.5 - center.z;
            return dx * dx + dy * dy + dz * dz;
        };
        auto const da = distSq(a), db = distSq(b);
        return da != db ? da < db : std::tuple{a.x, a.y, a.z} < std::tuple{b.x, b.y, b.z};
    });
    return result;
}

} // namespace lholo::projection::detail
