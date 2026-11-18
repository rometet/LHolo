// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "projection/world/ProjectionVirtualWorld.h"
#include "projection/world/ProjectionVirtualAirPolicy.h"

#include <tuple>
#include <utility>

#include "mc/world/level/BlockPos.h"

namespace lholo::projection::detail {
namespace {

thread_local ProjectionWorldLookup const* gTessellationLookup{};
thread_local ExpectedBlockActorMap const* gTessellationBlockActors{};
thread_local NativeLiquidTelemetry*       gNativeLiquidTelemetry{};
thread_local bool                         gSuppressRegionWrites{};
thread_local Block const*                 gDiodeAir{};
thread_local int                          gDiodeCenterX{}, gDiodeCenterY{}, gDiodeCenterZ{};

} // namespace

ScopedRegionWriteSuppression::ScopedRegionWriteSuppression()
: mPrevious(std::exchange(gSuppressRegionWrites, true)) {}

ScopedRegionWriteSuppression::~ScopedRegionWriteSuppression() {
    gSuppressRegionWrites = mPrevious;
}

bool regionWritesSuppressed() {
    return gSuppressRegionWrites;
}

ScopedTessellationBlocks::ScopedTessellationBlocks(
    ProjectionWorldLookup const& worldLookup,
    ExpectedBlockActorMap const& blockActors,
    NativeLiquidTelemetry*       telemetry
)
: mPreviousLookup(std::exchange(gTessellationLookup, &worldLookup)),
  mPreviousBlockActors(std::exchange(gTessellationBlockActors, &blockActors)),
  mPreviousTelemetry(std::exchange(gNativeLiquidTelemetry, telemetry)) {}

ScopedTessellationBlocks::~ScopedTessellationBlocks() {
    gTessellationLookup      = mPreviousLookup;
    gTessellationBlockActors = mPreviousBlockActors;
    gNativeLiquidTelemetry   = mPreviousTelemetry;
}

ScopedDiodeAirNeighborhood::ScopedDiodeAirNeighborhood(
    BlockPos const& center, Block const* air
) noexcept
: mPreviousAir(std::exchange(gDiodeAir, air)),
  mPreviousX(std::exchange(gDiodeCenterX, center.x)),
  mPreviousY(std::exchange(gDiodeCenterY, center.y)),
  mPreviousZ(std::exchange(gDiodeCenterZ, center.z)) {}

ScopedDiodeAirNeighborhood::~ScopedDiodeAirNeighborhood() noexcept {
    gDiodeAir = mPreviousAir;
    gDiodeCenterX = mPreviousX;
    gDiodeCenterY = mPreviousY;
    gDiodeCenterZ = mPreviousZ;
}

bool isDiodeVirtualAirPosition(BlockPos const& pos) noexcept {
    return gDiodeAir && inVirtualAirNeighborhood(
        pos.x, pos.y, pos.z,
        gDiodeCenterX, gDiodeCenterY, gDiodeCenterZ
    );
}

Block const* findTessellationBlock(BlockPos const& position) {
    if (!gTessellationLookup) return nullptr;
    auto const* found = gTessellationLookup->blockAt(
        std::tuple{position.x, position.y, position.z}
    );
    if (found) return found;
    return isDiodeVirtualAirPosition(position) ? gDiodeAir : nullptr;
}

Block const* findTessellationLiquid(BlockPos const& position) {
    if (!gTessellationLookup) return nullptr;
    if (gNativeLiquidTelemetry) ++gNativeLiquidTelemetry->virtualLiquidQueryHits;
    auto const* found = gTessellationLookup->liquidAt(
        std::tuple{position.x, position.y, position.z}
    );
    if (found) return found;
    return isDiodeVirtualAirPosition(position) ? gDiodeAir : nullptr;
}

BlockActor const* findTessellationBlockActor(BlockPos const& position) {
    if (!gTessellationBlockActors) return nullptr;
    auto const found = gTessellationBlockActors->find(
        std::tuple{position.x, position.y, position.z}
    );
    return found == gTessellationBlockActors->end() ? nullptr : found->second.get();
}

} // namespace lholo::projection::detail
