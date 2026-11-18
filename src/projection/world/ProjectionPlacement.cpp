// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "projection/world/ProjectionPlacement.h"
#include "app/RetainedObject.h"
#include "app/ScopeExit.h"
#include "app/NativeCallbackBoundary.h"

#include "projection/core/ProjectionRules.h"
#include "projection/core/ProjectionState.h"
#include "projection/world/ProjectionVirtualWorld.h"
#include "projection/runtime/ProjectionWorldEvents.h"
#include "structure/StructureLoader.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <chrono>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "mc/client/renderer/block/BlockTessellator.h"
#include "mc/client/renderer/blockactor/BlockActorRenderDispatcher.h"
#include "mc/dataloadhelper/NewUniqueIdsDataLoadHelper.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ILevel.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "mc/world/level/block/actor/VanillaBlockActorFactory.h"
#include "mc/world/level/block/actor/component/IVanillaRenderBlockActorComponent.h"
#include "mc/world/level/levelgen/structure/LegacyStructureSettings.h"

namespace lholo::projection::detail {
namespace {

void pairProjectedChests(BlockSource& region, ProjectionState& state) {
    ScopedRegionWriteSuppression projectedWrites;
    constexpr std::array<std::pair<int, int>, 4> horizontalNeighbors{{
        {-1, 0},
        {1, 0},
        {0, -1},
        {0, 1},
    }};

    ScopedTessellationBlocks projectedWorld{
        *state.worldLookup,
        *state.expectedWorldBlockActors
    };
    for (auto const& [key, actor] : *state.expectedWorldBlockActors) {
        if (!actor || actor->mType != BlockActorType::Chest) continue;
        auto* chest = static_cast<ChestBlockActor*>(actor.get());
        if (chest->mLargeChestPaired != nullptr) continue;

        auto const [x, y, z] = key;
        for (auto const [dx, dz] : horizontalNeighbors) {
            BlockPos const neighbor{x + dx, y, z + dz};
            auto const found = state.expectedWorldBlockActors->find(
                std::tuple{neighbor.x, neighbor.y, neighbor.z}
            );
            if (found == state.expectedWorldBlockActors->end()
                || !found->second
                || found->second->mType != BlockActorType::Chest) {
                continue;
            }

            chest->_tryToPairWith(region, neighbor);
            if (chest->mLargeChestPaired != nullptr) break;
        }
    }
}

} // namespace

bool refreshProjectionLayerView(
    ProjectionState& state, BlockSource& region,
    structure::LayerAxis axis, structure::LayerDisplayMode mode, int layer
) {
    if (!state.worldLookup || !state.blockTessellator || state.placementBuildActive)
        return false;
    // Publish a new immutable view so an already-running async tessellator
    // continues using its own captured layer. Palette transforms and actors
    // remain owned by the current placement; no full rebuild is needed.
    auto lookup = std::make_shared<ProjectionWorldLookup>(
        state.worldLookup->withLayer(axis, mode, layer));
    state.blockTessellator->mCachedGetBlock.get()
        = [lookup, regionAddress = &region](BlockPos const& position) -> Block const& {
            auto const* found = lookup->blockAt(
                std::tuple{position.x, position.y, position.z});
            return found ? *found : regionAddress->getBlock(position);
        };
    state.worldLookup = std::move(lookup);
    return true;
}

bool rebuildProjectionPlacement(
    ProjectionState&                   state,
    BlockSource&                       region,
    BlockActorRenderDispatcher&        dispatcher,
    LegacyStructureSettings const&     transformSettings,
    ProjectionPlacementSettings const& settings,
    bool                               restart
) {
    constexpr std::size_t kMinimumPlacementCellsPerFrame = 4096;
    constexpr std::size_t kMaximumPlacementCellsPerFrame = 16384;
    constexpr auto kPlacementWorkBudget = std::chrono::milliseconds(3);

    if (!state.structure || !state.level || !state.blockTessellator
        || !state.worldLookup || !state.expectedWorldBlockActors
        || state.correctionStates.size() < state.structure->renderBlocks.size()
        || state.blockActorRendererAvailable.size() < state.structure->renderBlocks.size()
        || state.blockToSection.size() < state.structure->renderBlocks.size()) {
        state.placementBuildActive = false;
        return false;
    }

    if (restart) {
        // Block queries/alpha rendering immediately. If preparation throws,
        // force a complete restart on the next opaque pass rather than leaving
        // cached settings paired with a partial virtual-world snapshot.
        state.placementBuildActive = true;
        bool prepared{};
        app::ScopeExit retry([&]() noexcept {
            if (!prepared) { state.cachedRotation = -1; state.cachedMirror = -1; }
        });
        publishProjectionWorldEventInterest(makeProjectionWorldEventInterest(
            *state.structure, {state.anchor.x + settings.offsetX, state.anchor.y + settings.offsetY,
                              state.anchor.z + settings.offsetZ}, settings.mirrorMode, settings.rotationTurns
        ));
        // A moved placement keeps its local GPU geometry, but the virtual world
        // and correction lookup must follow the new world origin.
        auto blockTessellator = std::make_unique<BlockTessellator>(&region);

        // Publish a fresh snapshot, then populate it incrementally. No mesh
        // worker is scheduled until the snapshot is complete, so workers never
        // observe partially built placement data. The immutable coordinate
        // view replaces the old per-cell world maps: it follows the new origin
        // without allocating one hash node per cell, and resolves every
        // distinct palette state once on this thread.
        auto actors = std::make_shared<ExpectedBlockActorMap>();
        actors->reserve(std::min<std::size_t>(
            state.structure->renderBlocks.size(), 1024
        ));
        ProjectionWorldLookup::BlockTransforms transformedBlocks;
        state.structure->renderBlocks.forEachStoredValue([&](auto const& cell) {
            for (auto value : {cell.block, cell.liquid}) {
                if (!value || transformedBlocks.contains(value)) continue;
                transformedBlocks.emplace(value, transformExpectedBlock(
                    value, transformSettings, settings.identityTransform
                ));
            }
        });
        auto worldLookup = std::make_shared<ProjectionWorldLookup>(
            state.structure,
            std::array<long long, 3>{
                static_cast<long long>(state.anchor.x) + settings.offsetX,
                static_cast<long long>(state.anchor.y) + settings.offsetY,
                static_cast<long long>(state.anchor.z) + settings.offsetZ
            },
            settings.mirrorMode, settings.rotationTurns,
            settings.layerAxis, settings.layerDisplayMode, settings.displayLayer,
            std::move(transformedBlocks)
        );
        std::vector<Vec3> centerSums(state.sections.size());
        std::vector<std::size_t> centerCounts(state.sections.size());

        // All allocations succeeded. Retire borrowed records before owners;
        // the following publication uses only nonthrowing moves/clears.
        state.projectedBlockActors.clear();
        state.blockTessellator = std::move(blockTessellator);
        state.worldLookup = std::move(worldLookup);
        state.expectedWorldBlockActors = std::move(actors);
        std::fill(
            state.blockActorRendererAvailable.begin(),
            state.blockActorRendererAvailable.end(),
            0
        );
        state.placementCenterSums = std::move(centerSums);
        state.placementCenterCounts = std::move(centerCounts);
        state.placementBuildCursor = 0;
        state.placementBuildActive = true;
        prepared = true;
    }

    if (!state.placementBuildActive) return true;

    auto const total = state.structure->renderBlocks.size();
    auto const begin = state.placementBuildCursor;
    auto const hardEnd = std::min(total, begin + kMaximumPlacementCellsPerFrame);
    auto const frameStarted = std::chrono::steady_clock::now();

    for (std::size_t index = begin; index < hardEnd; ++index) {
        // Give fast machines up to 4x the old per-frame progress, but cap the
        // time spent on a single render callback once the old minimum is met.
        if (index - begin >= kMinimumPlacementCellsPerFrame
            && (index - begin) % 256 == 0
            && std::chrono::steady_clock::now() - frameStarted >= kPlacementWorkBudget) {
            break;
        }
        auto const& entry = state.structure->renderBlocks[index];
        bool const visible = isLayerVisible(
            projectionLayer(*state.structure, entry, settings.layerAxis, settings.mirrorMode, settings.rotationTurns),
            settings.layerDisplayMode, settings.displayLayer,
            entry.materialIndex, entry.liquidMaterialIndex, settings.layerAxis);
        // Retain native actors for *all* layers. A later layer-only switch
        // changes just the immutable lookup view and visibility bitmask; it
        // cannot safely construct native actors from the mesh worker.
        bool const mayHaveActor = !visible && entry.block
            && entry.block->getBlockType().getBlockEntityType() != BlockActorType::Undefined;
        if (!visible && !mayHaveActor) {
            state.correctionStates.set(index, CorrectionState::Correct);
            state.placementBuildCursor = index + 1;
            continue;
        }
        auto const transformed = transformStructurePosition(
            entry, *state.structure, settings.mirrorMode, settings.rotationTurns
        );
        auto const* transformedBlock = transformExpectedBlock(
            entry.block, transformSettings, settings.identityTransform
        );
        BlockPos const worldPosition{
            state.anchor.x + settings.offsetX + transformed.x,
            state.anchor.y + settings.offsetY + transformed.y,
            state.anchor.z + settings.offsetZ + transformed.z
        };
        auto const worldKey = std::tuple{worldPosition.x, worldPosition.y, worldPosition.z};
        if (transformedBlock) {
            if (transformedBlock->getBlockType().getBlockEntityType() != BlockActorType::Undefined) {
                try {
                    auto blockActor = VanillaBlockActorFactory::createBlockActor(
                        worldPosition, transformedBlock->getBlockType()
                    );
                    if (blockActor) {
                        if (entry.blockEntityNbt) {
                            NewUniqueIdsDataLoadHelper dataLoadHelper;
                            dataLoadHelper.mLevel = state.level;
                            blockActor->load(*state.level, *entry.blockEntityNbt, dataLoadHelper);
                            blockActor->mPosition = worldPosition;
                        }
                        auto const retained = app::retainOwnedObject(
                            *state.expectedWorldBlockActors, worldKey, std::move(blockActor)
                        );
                        auto* actor = retained.first;
                        auto* renderComponent = state.blockActorRendererAvailable[index]
                            ? nullptr : actor->_getRenderComponent();
                        if (renderComponent) {
                            auto const rendererId = renderComponent->getRendererId();
                            auto const rendererIndex = static_cast<unsigned int>(rendererId);
                            if (rendererIndex
                                    < static_cast<unsigned int>(BlockActorRendererId::Count)
                                && dispatcher.mRenderers.get()[rendererId]) {
                                auto const& typeName = transformedBlock->getTypeName();
                                bool const pistonCandidate = typeName == "minecraft:piston"
                                    || typeName == "minecraft:sticky_piston";
                                state.projectedBlockActors.push_back({
                                    worldPosition, transformedBlock, actor, index, pistonCandidate
                                });
                                state.blockActorRendererAvailable[index] = 1;
                            }
                        }
                    }
                } catch (std::exception const& exception) {
                    app::reportNativeCallbackFailure("projected block actor", exception.what());
                } catch (...) {
                    // Block-entity NBT is file-controlled. A malformed or
                    // version-incompatible actor must not unwind through the
                    // render hook; the ghost block itself remains renderable.
                    app::reportNativeCallbackFailure("projected block actor", "unknown C++ exception");
                }
            }
        }

        if (!visible) {
            // Retain the actor for future layer toggles, but keep hidden
            // cells out of placement centers and correction rendering.
            state.correctionStates.set(index, CorrectionState::Correct);
            state.placementBuildCursor = index + 1;
            continue;
        }
        auto const section = state.blockToSection[index];
        if (section < state.placementCenterSums.size()) {
            state.placementCenterSums[section] += Vec3{
                static_cast<float>(transformed.x) + 0.5f,
                static_cast<float>(transformed.y) + 0.5f,
                static_cast<float>(transformed.z) + 0.5f
            };
            ++state.placementCenterCounts[section];
        }
        // Earlier successful cells must not be replayed/count their centers
        // twice if a later cell's map allocation fails in this frame.
        state.placementBuildCursor = index + 1;
    }

    if (state.placementBuildCursor < total) return false;

    pairProjectedChests(region, state);
    for (std::size_t section = 0; section < state.sections.size(); ++section) {
        if (section < state.placementCenterCounts.size()
            && state.placementCenterCounts[section] != 0) {
            state.sections[section].center
                = state.placementCenterSums[section]
                / static_cast<float>(state.placementCenterCounts[section]);
        }
    }

    auto worldLookup = state.worldLookup;
    auto* regionAddress = &region;
    state.blockTessellator->mCachedGetBlock.get()
        = [worldLookup = std::move(worldLookup), regionAddress](
              BlockPos const& position
          ) -> Block const& {
            auto const* found = worldLookup->blockAt(
                std::tuple{position.x, position.y, position.z}
            );
            return !found
                ? regionAddress->getBlock(position) : *found;
        };

    state.correctionScan.reset(state.sectionBlockIndices);
    state.correctionEvents.clear();
    state.correctionSubChunks.clear();
    state.initialCorrectionScan.reset(state.structure->renderBlocks.size());
    state.placementBuildActive = false;
    state.placementCenterSums.clear();
    state.placementCenterCounts.clear();
    return true;
}

} // namespace lholo::projection::detail
