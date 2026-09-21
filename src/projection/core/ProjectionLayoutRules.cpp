// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Minecraft-free projection layout rules. This translation unit only depends
// on header-only Minecraft enums and pure LHolo data types, so the logic
// tests can link it without the game runtime.

#include "projection/core/ProjectionRules.h"

#include <algorithm>
#include <cmath>

namespace lholo::projection::detail {

RenderBucket renderBucketFor(BlockRenderLayer layer) {
    switch (layer) {
    case BlockRenderLayer::RenderlayerBlend:
    case BlockRenderLayer::RenderlayerBlendToOpaque:
        return RenderBucket::Blend;
    case BlockRenderLayer::RenderlayerOpaque:
    case BlockRenderLayer::RenderlayerSeasonsOpaque:
    case BlockRenderLayer::RenderlayerShiftOpaqueInternalOnly:
        return RenderBucket::Opaque;
    case BlockRenderLayer::RenderlayerAlphatestSingleSide:
    case BlockRenderLayer::RenderlayerAlphatestSingleSideToOpaque:
    case BlockRenderLayer::RenderlayerShiftAlphatestSingleSideInternalOnly:
    case BlockRenderLayer::RenderlayerShiftAlphatestSingleSideToOpaqueInternalOnly:
        return RenderBucket::AlphaOneSided;
    default:
        return RenderBucket::Alpha;
    }
}

std::uint32_t applyGhostAppearanceAbgr(
    std::uint32_t nativeColor,
    float         opacity,
    float         blueTint
) {
    auto const normalizedOpacity = std::isfinite(opacity)
        ? std::clamp(opacity, 0.0f, 1.0f)
        : 1.0f;
    auto const normalizedTint = std::isfinite(blueTint)
        ? std::clamp(blueTint, 0.0f, 1.0f)
        : 0.0f;
    auto const multiply = [&](unsigned int shift, float factor) {
        return static_cast<std::uint32_t>(
            static_cast<float>((nativeColor >> shift) & 0xFFU) * factor
        ) << shift;
    };
    // Keep these factors synchronized with SchematicVisuals::ghost().
    return multiply(0, 1.0f - 0.25f * normalizedTint)
        | multiply(8, 1.0f - 0.15f * normalizedTint)
        | multiply(16, 1.0f)
        | multiply(24, normalizedOpacity);
}

Mirror getProjectionMirror(int mirrorMode) {
    switch (mirrorMode) {
    // LHolo's UI names the coordinate being flipped. Bedrock names Mirror by
    // the axis kept fixed: Mirror::Z flips X, while Mirror::X flips Z.
    case 1: return Mirror::Z;
    case 2: return Mirror::X;
    default: return Mirror::None;
    }
}

Rotation getProjectionRotation(int quarterTurns) {
    switch (quarterTurns & 3) {
    case 1: return Rotation::Clockwise90;
    case 2: return Rotation::Clockwise180;
    case 3: return Rotation::CounterClockwise90;
    default: return Rotation::None;
    }
}

BlockPos transformStructurePosition(
    structure::LoadedStructure::RenderBlock const& entry,
    structure::LoadedStructure const&              loaded,
    int                                             mirrorMode,
    int                                             rotation
) {
    return transformStructurePosition(
        BlockPos{entry.x, entry.y, entry.z}, loaded, mirrorMode, rotation
    );
}

BlockPos transformStructurePosition(
    BlockPos const&                   position,
    structure::LoadedStructure const& loaded,
    int                               mirrorMode,
    int                               rotation
) {
    int x = position.x;
    int z = position.z;
    if (mirrorMode == 1) x = loaded.sizeX - 1 - x;
    if (mirrorMode == 2) z = loaded.sizeZ - 1 - z;
    switch (rotation) {
    case 1: return BlockPos{loaded.sizeZ - 1 - z, position.y, x};
    case 2: return BlockPos{loaded.sizeX - 1 - x, position.y, loaded.sizeZ - 1 - z};
    case 3: return BlockPos{z, position.y, loaded.sizeX - 1 - x};
    default: return BlockPos{x, position.y, z};
    }
}

BlockPos inverseTransformStructurePosition(
    BlockPos const&                   position,
    structure::LoadedStructure const& loaded,
    int                               mirrorMode,
    int                               rotation
) {
    int x{};
    int z{};
    switch (rotation & 3) {
    case 1:
        x = position.z;
        z = loaded.sizeZ - 1 - position.x;
        break;
    case 2:
        x = loaded.sizeX - 1 - position.x;
        z = loaded.sizeZ - 1 - position.z;
        break;
    case 3:
        x = loaded.sizeX - 1 - position.z;
        z = position.x;
        break;
    default:
        x = position.x;
        z = position.z;
        break;
    }
    if (mirrorMode == 1) x = loaded.sizeX - 1 - x;
    if (mirrorMode == 2) z = loaded.sizeZ - 1 - z;
    return BlockPos{x, position.y, z};
}

bool isStructureCellCovered(
    structure::LoadedStructure const& loaded,
    BlockPos const&                    position
) {
    for (auto const& region : loaded.regions) {
        if (position.x >= region.x && position.x < region.x + region.sizeX
            && position.y >= region.y && position.y < region.y + region.sizeY
            && position.z >= region.z && position.z < region.z + region.sizeZ) {
            return true;
        }
    }
    return false;
}

bool isLayerVisible(
    int layer,
    structure::LayerDisplayMode layerDisplayMode,
    int displayLayer,
    int materialIndex,
    int secondaryMaterialIndex,
    structure::LayerAxis layerAxis
) {
    if (layerAxis == structure::LayerAxis::Material) {
        // Full material view is still a full projection view. Extra world
        // blocks have no schematic material index, but correction must not
        // disappear merely because the UI grouping is set to materials.
        if (layerDisplayMode == structure::LayerDisplayMode::All) return true;
        auto const materialVisible = [&](int index) {
            if (index < 0) return false;
            switch (layerDisplayMode) {
            case structure::LayerDisplayMode::Single: return index == displayLayer;
            case structure::LayerDisplayMode::UpToCurrent: return index <= displayLayer;
            case structure::LayerDisplayMode::FromCurrent: return index >= displayLayer;
            default: return true;
            }
        };
        return materialVisible(materialIndex) || materialVisible(secondaryMaterialIndex);
    }
    switch (layerDisplayMode) {
    case structure::LayerDisplayMode::Single: return layer == displayLayer;
    case structure::LayerDisplayMode::UpToCurrent: return layer <= displayLayer;
    case structure::LayerDisplayMode::FromCurrent: return layer >= displayLayer;
    default: return true;
    }
}

} // namespace lholo::projection::detail
