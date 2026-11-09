#pragma once

#include "structure/StructureLoader.h"
#include "projection/core/ProjectionRules.h"

#include <array>
#include <memory>
#include <optional>
#include <tuple>
#include <unordered_map>

namespace lholo::projection::detail {

// One immutable placement view, shared by the active projection and workers.
// Coordinates are inverted on demand; moving a structure never duplicates a
// hash node for each of its cells. Native state transforms are resolved once
// per distinct Block on the game thread, never inside worker queries.
class ProjectionWorldLookup {
public:
    using Key = std::tuple<int, int, int>;
    using BlockTransforms = std::unordered_map<Block const*, Block const*>;
    ProjectionWorldLookup() = default;
    ProjectionWorldLookup(std::shared_ptr<structure::LoadedStructure const> source,
                          std::array<long long, 3> origin, int mirror, int rotation,
                          structure::LayerAxis axis, structure::LayerDisplayMode mode,
                          int layer,
                          BlockTransforms blocks)
        : mSource(std::move(source)), mOrigin(origin), mMirror(mirror), mRotation(rotation),
          mAxis(axis), mMode(mode), mLayer(layer), mBlocks(std::move(blocks)) {}

    std::optional<std::size_t> indexAt(Key const& key) const {
        if (!mSource) return {};
        auto [x, y, z] = key;
        std::array<long long, 3> p{x - mOrigin[0], y - mOrigin[1], z - mOrigin[2]};
        bool const rotated = (mRotation & 1) != 0;
        if (p[0] < 0 || p[1] < 0 || p[2] < 0 ||
            p[0] >= (rotated ? mSource->sizeZ : mSource->sizeX) || p[1] >= mSource->sizeY ||
            p[2] >= (rotated ? mSource->sizeX : mSource->sizeZ)) return {};
        auto local = inverseTransformStructurePosition(
            BlockPos{static_cast<int>(p[0]), static_cast<int>(p[1]), static_cast<int>(p[2])},
            *mSource, mMirror, mRotation
        );
        auto index = mSource->renderBlocks.find(local.x, local.y, local.z);
        if (!index) return {};
        auto cell = mSource->renderBlocks[*index];
        // Hidden layers behave like completed cells for mesh generation and
        // stay excluded from the world lookup, exactly like the placement loop.
        if (!isLayerVisible(projectionLayer(*mSource, cell, mAxis, mMirror, mRotation),
                mMode, mLayer, cell.materialIndex, cell.liquidMaterialIndex, mAxis)) return {};
        return index;
    }
    // Bedrock stores the body and liquid layers independently; waterlogged
    // cells keep both. Each layer resolves through the same stored transform.
    Block const* blockAt(Key const& key) const { return valueAt(key, false); }
    Block const* liquidAt(Key const& key) const { return valueAt(key, true); }
private:
    Block const* valueAt(Key const& key, bool liquid) const {
        auto index = indexAt(key);
        if (!index) return nullptr;
        auto cell = mSource->renderBlocks[*index];
        auto value = liquid ? cell.liquid : cell.block;
        if (!value) return nullptr;
        // Placement resolves every source palette state before publication.
        // A missing transform is an invariant failure, never schematic air.
        return mBlocks.at(value);
    }
    std::shared_ptr<structure::LoadedStructure const> mSource;
    std::array<long long, 3> mOrigin{};
    int mMirror{}, mRotation{};
    structure::LayerAxis mAxis{structure::LayerAxis::Y};
    structure::LayerDisplayMode mMode{};
    int mLayer{};
    BlockTransforms mBlocks;
};
} // namespace lholo::projection::detail
