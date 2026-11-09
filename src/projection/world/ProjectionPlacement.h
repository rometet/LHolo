// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Rebuilds the immutable projected-world view after placement, transform, or
// layer changes. Rendering and worker scheduling remain outside this module.

#pragma once

#include "structure/LayerDisplayTypes.h"

class BlockActorRenderDispatcher;
class BlockSource;
class LegacyStructureSettings;

namespace lholo::projection::detail {

struct ProjectionState;

struct ProjectionPlacementSettings {
    int  mirrorMode{};
    int  rotationTurns{};
    int  offsetX{};
    int  offsetY{};
    int  offsetZ{};
    structure::LayerDisplayMode layerDisplayMode{structure::LayerDisplayMode::All};
    int  displayLayer{};
    structure::LayerAxis layerAxis{structure::LayerAxis::Y};
    bool identityTransform{};
};

// Layer-only fast path: retain the resolved palette, actors, and world pose;
// publish a new immutable view without replaying every schematic block.
bool refreshProjectionLayerView(
    ProjectionState& state, BlockSource& region,
    structure::LayerAxis axis, structure::LayerDisplayMode mode, int layer
);

bool rebuildProjectionPlacement(
    ProjectionState&                   state,
    BlockSource&                       region,
    BlockActorRenderDispatcher&        dispatcher,
    LegacyStructureSettings const&     transformSettings,
    ProjectionPlacementSettings const& settings,
    bool                               restart
);

} // namespace lholo::projection::detail
