// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// CPU geometry construction shared by the asynchronous worker and synchronous
// compatibility fallback.

#pragma once

#include <cstddef>

#include "mc/client/renderer/Tessellator.h"
#include "mc/util/Mirror.h"
#include "mc/util/Rotation.h"

class BlockSource;
class BlockTessellator;

namespace lholo::projection::detail {

struct ProjectionState;

struct ProjectionSectionBuildSettings {
    Mirror   mirror{Mirror::None};
    Rotation rotation{Rotation::None};
    int      mirrorMode{};
    int      rotationTurns{};
    int      offsetX{};
    int      offsetY{};
    int      offsetZ{};
    float    structureOpacity{};
    float    correctionFillOpacity{};
    float    correctionOutlineOpacity{};
    bool     identityTransform{};
};

// Shared by the async owner's lightweight setup and synchronous section build.
// Never mode leaves bounds to the render owner; an existing mesh is reused.
void buildStructureBoundsMesh(
    ProjectionState& state, Tessellator& tessellator, Tessellator::UploadMode uploadMode,
    ProjectionSectionBuildSettings const& settings
);

void buildProjectionSection(
    ProjectionState&                      state,
    Tessellator&                          tessellator,
    BlockTessellator&                     blockTessellator,
    BlockSource&                          region,
    std::size_t                           section,
    Tessellator::UploadMode               uploadMode,
    ProjectionSectionBuildSettings const& settings
);

} // namespace lholo::projection::detail
