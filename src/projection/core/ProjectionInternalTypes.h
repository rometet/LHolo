// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Internal value types shared by projection implementation units. These types
// own no game or rendering resources and contain no behavior.

#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <map>
#include <memory>
#include <tuple>

#include "mc/world/level/BlockPos.h"

class Block;
class BlockActor;

namespace lholo::projection::detail {

using SubChunkKey = std::tuple<int, int, int>;

enum class CorrectionState : std::uint8_t { Unknown, Missing, Correct, WrongType, WrongState };
enum class RenderBucket : std::uint8_t { Opaque, Alpha, AlphaOneSided, Blend, Count };

// Cumulative Phase-2 counters. Async section builds accumulate into their
// snapshot and merge into the active state only after a matching revision is
// accepted, so discarded worker results never claim a native-liquid success.
struct NativeLiquidTelemetry {
    std::uint64_t nativeLiquidCellsAttempted{};
    std::uint64_t nativeLiquidTessellationPositive{};
    std::uint64_t nativeLiquidTessellationZero{};
    std::uint64_t nativeLiquidTessellationFailure{};
    std::uint64_t nativeLiquidVertices{};
    std::uint64_t nativeLiquidUvVertices{};
    std::uint64_t nativeLiquidColorVertices{};
    std::uint64_t nativeLiquidAlphaModifiedVertices{};
    std::uint64_t virtualLiquidQueryHits{};
    std::uint64_t nativeLiquidSignTextResolved{};
    std::uint64_t nativeLiquidSignTextDraws{};
    std::uint64_t nativeLiquidTerrainBlendResolved{};
    std::uint64_t nativeLiquidTerrainBlendDraws{};
    std::uint64_t nativeLiquidLegacyMaterialDraws{};
    std::uint64_t liquidProxyFallbackCells{};
    std::uint64_t liquidProxyDrawCells{};
    std::array<std::uint64_t, 22> nativeLiquidLayerAttempts{};
};

struct ProjectedBlockActor {
    BlockPos     position{};
    Block const* block{};
    BlockActor*  actor{};
    std::size_t  structureIndex{};
};

using ExpectedBlockMap      = std::map<SubChunkKey, Block const*>;
using ExpectedLiquidMap     = std::map<SubChunkKey, Block const*>;
using ExpectedBlockActorMap = std::map<SubChunkKey, std::shared_ptr<BlockActor>>;
using ExpectedBlockIndexMap = std::map<SubChunkKey, std::size_t>;

} // namespace lholo::projection::detail
