// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Minecraft-free projection layout rules. This translation unit only depends
// on header-only Minecraft enums and pure LHolo data types, so the logic
// tests can link it without the game runtime.

#include "projection/core/ProjectionRules.h"
#include "projection/core/ProjectionVisibilityRules.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lholo::projection::detail {
namespace {
int representableCoordinate(std::int64_t value) {
    return static_cast<int>(std::clamp(value, static_cast<std::int64_t>((std::numeric_limits<int>::min)()),
        static_cast<std::int64_t>((std::numeric_limits<int>::max)())));
}
} // namespace

ProjectionWorldBounds projectionSectionCellWorldBounds(SubChunkKey const& key,
    structure::LoadedStructure const& loaded, std::array<long long, 3> const& origin,
    int mirror, int rotation, int halo) {
    auto [x, y, z] = key;
    std::array<int, 3> section{x, y, z}, size{loaded.sizeX, loaded.sizeY, loaded.sizeZ}, lo{}, hi{};
    for (int axis = 0; axis < 3; ++axis) {
        auto start = static_cast<long long>(section[axis]) * 16;
        lo[axis] = static_cast<int>(std::clamp(start - halo, 0LL, static_cast<long long>(size[axis])));
        hi[axis] = static_cast<int>(std::clamp(start + 16 + halo, 0LL, static_cast<long long>(size[axis])));
        if (lo[axis] >= hi[axis]) return {};
    }
    auto a = transformStructurePosition(BlockPos{lo[0], lo[1], lo[2]}, loaded, mirror, rotation);
    auto b = transformStructurePosition(BlockPos{hi[0] - 1, hi[1] - 1, hi[2] - 1}, loaded, mirror, rotation);
    return {{origin[0] + std::min(a.x, b.x), origin[1] + std::min(a.y, b.y), origin[2] + std::min(a.z, b.z)},
            {origin[0] + std::max(a.x, b.x) + 1, origin[1] + std::max(a.y, b.y) + 1,
             origin[2] + std::max(a.z, b.z) + 1}};
}

ProjectionBounds projectionSectionBounds(
    SubChunkKey const& key, structure::LoadedStructure const& loaded, int mirror, int rotation
) {
    auto const [sx,sy,sz]=key;
    ProjectionBounds bounds;
    for(int dx : {0,15}) for(int dy : {0,15}) for(int dz : {0,15}) {
        auto const p=transformStructurePosition(
            BlockPos{sx*16+dx,sy*16+dy,sz*16+dz},loaded,mirror,rotation);
        bounds.include({static_cast<float>(p.x),static_cast<float>(p.y),static_cast<float>(p.z)});
    }
    bounds.maximum={bounds.maximum.x+1.0f,bounds.maximum.y+1.0f,bounds.maximum.z+1.0f};
    return bounds;
}

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
    auto const cell = structure::PlacementTransform{{loaded.sizeX,loaded.sizeY,loaded.sizeZ},{},rotation,mirrorMode}
        .toWorld({position.x,position.y,position.z});
    return BlockPos{representableCoordinate(cell.x),position.y,representableCoordinate(cell.z)};
}

BlockPos inverseTransformStructurePosition(
    BlockPos const&                   position,
    structure::LoadedStructure const& loaded,
    int                               mirrorMode,
    int                               rotation
) {
    // Neighbor queries may be outside [0,size). Use wide arithmetic and retain
    // an out-of-volume sentinel at int limits when the exact result cannot fit.
    auto const cell = structure::PlacementTransform{{loaded.sizeX,loaded.sizeY,loaded.sizeZ},{},rotation,mirrorMode}
        .toLocal({position.x,position.y,position.z});
    return BlockPos{representableCoordinate(cell.x),position.y,representableCoordinate(cell.z)};
}

std::optional<ProjectionCellBox> projectionSectionLocalIntersection(
    std::array<int, 3> const& section, structure::LoadedStructure const& loaded,
    BlockPos const& origin, int mirror, int rotation
) {
    bool rotated = (rotation & 1) != 0;
    std::array<int, 3> size{rotated ? loaded.sizeZ : loaded.sizeX, loaded.sizeY, rotated ? loaded.sizeX : loaded.sizeZ};
    std::array<long long, 3> offset{origin.x, origin.y, origin.z};
    std::array<int, 3> lo{}, hi{};
    for (int axis = 0; axis < 3; ++axis) {
        auto base = static_cast<long long>(section[axis]) * 16;
        auto minimum = std::max(base, offset[axis]);
        auto maximum = std::min(base + 15, offset[axis] + size[axis] - 1);
        if (minimum > maximum) return std::nullopt;
        lo[axis] = static_cast<int>(minimum - offset[axis]);
        hi[axis] = static_cast<int>(maximum - offset[axis]);
    }
    auto a = inverseTransformStructurePosition(BlockPos{lo[0], lo[1], lo[2]}, loaded, mirror, rotation);
    auto b = inverseTransformStructurePosition(BlockPos{hi[0], hi[1], hi[2]}, loaded, mirror, rotation);
    return ProjectionCellBox{{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)},
        {std::max(a.x, b.x) + 1, std::max(a.y, b.y) + 1, std::max(a.z, b.z) + 1}};
}

bool isStructureCellCovered(
    structure::LoadedStructure const& loaded,
    BlockPos const&                    position
) {
    for (auto const& region : loaded.regions) {
        if (position.x >= region.x && static_cast<std::int64_t>(position.x) < static_cast<std::int64_t>(region.x) + region.sizeX
            && position.y >= region.y && static_cast<std::int64_t>(position.y) < static_cast<std::int64_t>(region.y) + region.sizeY
            && position.z >= region.z && static_cast<std::int64_t>(position.z) < static_cast<std::int64_t>(region.z) + region.sizeZ) {
            return true;
        }
    }
    return false;
}

WorldEventInterest makeProjectionWorldEventInterest(
    structure::LoadedStructure const& loaded, std::array<int, 3> const& origin,
    int mirrorMode, int rotation
) {
    std::vector<WorldEventInterest::Box> boxes;
    boxes.reserve(loaded.regions.size());
    for (auto const& region : loaded.regions) {
        if (region.sizeX <= 0 || region.sizeY <= 0 || region.sizeZ <= 0) continue;
        auto const first = transformStructurePosition(BlockPos{region.x, region.y, region.z}, loaded, mirrorMode, rotation);
        auto const last = transformStructurePosition(BlockPos{
            representableCoordinate(static_cast<std::int64_t>(region.x) + region.sizeX - 1),
            representableCoordinate(static_cast<std::int64_t>(region.y) + region.sizeY - 1),
            representableCoordinate(static_cast<std::int64_t>(region.z) + region.sizeZ - 1)
        }, loaded, mirrorMode, rotation);
        std::array<int, 3> const a{first.x, first.y, first.z}, b{last.x, last.y, last.z};
        WorldEventInterest::Box box;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            box.min[axis] = static_cast<std::int64_t>(origin[axis]) + (std::min)(a[axis], b[axis]);
            box.max[axis] = static_cast<std::int64_t>(origin[axis]) + (std::max)(a[axis], b[axis]) + 1;
        }
        boxes.push_back(box);
    }
    return WorldEventInterest{std::move(boxes)};
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
