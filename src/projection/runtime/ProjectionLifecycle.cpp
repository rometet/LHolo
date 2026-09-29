// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/runtime/ProjectionLifecycle.h"

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/mesh/ProjectionMeshWorker.h"
#include "projection/runtime/ProjectionProgress.h"
#include "projection/core/ProjectionState.h"
#include "projection/section/ProjectionSectionStateStore.h"
#include "projection/runtime/ProjectionWorldEvents.h"
#include "structure/StructureLoader.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <limits>
#include <map>
#include <tuple>
#include <utility>

#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/client/renderer/block/BlockTessellator.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/deps/minecraft_renderer/renderer/BedrockTextureData.h"
#include "mc/deps/minecraft_renderer/renderer/IsMissingTexture.h"
#include "mc/world/actor/Actor.h"

namespace lholo::projection::detail {
namespace {

std::atomic_uint64_t sProjectionActivationGeneration{};

enum class ProjectionReleaseScope : unsigned char {
    Dimension,
    World,
};

void releaseProjectionState(ProjectionState& state, ProjectionReleaseScope scope) {
    // Keep teardown ordering in one place: workers may retain dimension-owned
    // snapshots, so they must finish before listeners and state are released.
    stopMeshWorker();
    if (scope == ProjectionReleaseScope::Dimension) {
        detachProjectionDimensionEvents();
    } else {
        detachProjectionWorldEvents();
    }
    resetPublishedBuildProgress();
    state = ProjectionState{};
}

bool resolveTerrainTexture(IClientInstance& client, ProjectionState& state) {
    auto* levelRenderer = client.getLevelRenderer();
    if (!levelRenderer) return false;
    auto const& atlasTexture = levelRenderer->mAtlasTexture.get();
    auto const& atlasData = atlasTexture.mClientTexture;
    if (!atlasData || atlasData->mIsMissingTexture == IsMissingTexture::Yes) return false;
    state.terrainTexture.emplace(atlasTexture);
    state.terrainTextureVariant.emplace(*state.terrainTexture);
    return true;
}

} // namespace

bool prepareProjectionState(
    ProjectionState&                              state,
    BaseActorRenderContext&                       renderContext,
    std::shared_ptr<structure::LoadedStructure const> loaded
) {
    auto& client = renderContext.mClientInstance;
    auto* player = client.getLocalPlayer();
    if (!player || !loaded || loaded->renderBlocks.empty()) return false;

    state.client = &client;
    state.level = &player->getLevel();
    state.dimension = &player->getDimension();
    state.dimensionId = static_cast<int>(player->getDimensionId());
    state.structure = std::move(loaded);
    state.structureGeneration = state.structure->generation;
    state.activationGeneration = sProjectionActivationGeneration.fetch_add(
        1,
        std::memory_order_relaxed
    ) + 1;
    state.blockTessellator = std::make_unique<BlockTessellator>(
        &player->getDimensionBlockSource()
    );
    state.correctionStates.resize(
        state.structure->renderBlocks.size(), CorrectionState::Unknown
    );
    state.progressCorrect.resize(state.structure->renderBlocks.size(), 0);
    state.progressErrorKind.resize(state.structure->renderBlocks.size(), 0);
    state.blockActorRendererAvailable.resize(state.structure->renderBlocks.size(), 0);
    // Force the first render pass to build the transformed virtual-world lookup.
    state.cachedRotation = -1;
    state.cachedMirror = -1;

    std::vector<Vec3> centers;
    auto const blockCount = state.structure->renderBlocks.size();
    state.blockToSection.resize(blockCount);

    // Structure loaders normalize render-block coordinates into [0, size).
    // Use a dense section lookup during initial grouping instead of doing one
    // std::map tree lookup/allocation per block. The persistent map is still
    // populated once per occupied section for later sparse correction lookups.
    auto const sectionCountX = static_cast<std::size_t>((state.structure->sizeX + 15) / 16);
    auto const sectionCountY = static_cast<std::size_t>((state.structure->sizeY + 15) / 16);
    auto const sectionCountZ = static_cast<std::size_t>((state.structure->sizeZ + 15) / 16);
    auto const denseSectionCount64 =
        static_cast<std::uint64_t>(sectionCountX)
        * static_cast<std::uint64_t>(sectionCountY)
        * static_cast<std::uint64_t>(sectionCountZ);
    constexpr std::uint64_t kDenseSectionLookupLimit = 1U << 20;
    constexpr auto NoSection = std::numeric_limits<std::size_t>::max();

    if (denseSectionCount64 != 0
        && denseSectionCount64 <= kDenseSectionLookupLimit) {
        std::vector<std::size_t> denseLookup(
            static_cast<std::size_t>(denseSectionCount64),
            NoSection
        );
        std::vector<std::size_t> sectionCounts;
        centers.reserve(std::min<std::size_t>(
            blockCount, static_cast<std::size_t>(denseSectionCount64)
        ));
        state.localSectionKeys.reserve(centers.capacity());

        auto const denseIndex = [&](int x, int y, int z) {
            auto const sx = static_cast<std::size_t>(x / 16);
            auto const sy = static_cast<std::size_t>(y / 16);
            auto const sz = static_cast<std::size_t>(z / 16);
            return (sx * sectionCountY + sy) * sectionCountZ + sz;
        };

        // Pass 1: compact occupied dense sections and count their blocks.
        for (auto const& entry : state.structure->renderBlocks) {
            auto const slot = denseIndex(entry.x, entry.y, entry.z);
            auto section = denseLookup[slot];
            if (section == NoSection) {
                section = centers.size();
                denseLookup[slot] = section;
                auto const key = std::tuple{
                    entry.x / 16,
                    entry.y / 16,
                    entry.z / 16
                };
                state.localSectionIndices.emplace(key, section);
                state.localSectionKeys.push_back(key);
                auto const [sx, sy, sz] = key;
                centers.emplace_back(
                    static_cast<float>(sx * 16 + 8),
                    static_cast<float>(sy * 16 + 8),
                    static_cast<float>(sz * 16 + 8)
                );
                sectionCounts.push_back(0);
            }
            ++sectionCounts[section];
        }

        state.sectionBlockIndices.resize(centers.size());
        for (std::size_t section = 0; section < centers.size(); ++section) {
            state.sectionBlockIndices[section].reserve(sectionCounts[section]);
        }

        // Pass 2: direct O(1) assignment with no inner-vector reallocations.
        for (std::size_t index = 0; index < blockCount; ++index) {
            auto const& entry = state.structure->renderBlocks[index];
            auto const section = denseLookup[denseIndex(entry.x, entry.y, entry.z)];
            state.blockToSection[index] = section;
            state.sectionBlockIndices[section].push_back(index);
        }
    } else {
        // Extremely sparse/huge extents avoid allocating an oversized dense
        // table and retain the original sparse-map behavior.
        for (std::size_t index = 0; index < blockCount; ++index) {
            auto const& entry = state.structure->renderBlocks[index];
            auto const key = std::tuple{entry.x / 16, entry.y / 16, entry.z / 16};
            auto [found, inserted] = state.localSectionIndices.try_emplace(
                key, state.sectionBlockIndices.size()
            );
            if (inserted) {
                state.sectionBlockIndices.emplace_back();
                state.localSectionKeys.push_back(key);
                auto const [sx, sy, sz] = key;
                centers.emplace_back(
                    static_cast<float>(sx * 16 + 8),
                    static_cast<float>(sy * 16 + 8),
                    static_cast<float>(sz * 16 + 8)
                );
            }
            state.blockToSection[index] = found->second;
            state.sectionBlockIndices[found->second].push_back(index);
        }
    }
    initializeSectionStates(state.sections, centers);
    state.warningFillSectionMeshes.resize(state.sectionBlockIndices.size());
    state.correctionOutlineSectionMeshes.resize(state.sectionBlockIndices.size());
    state.wrongFillSectionMeshes.resize(state.sectionBlockIndices.size());
    state.wrongOutlineSectionMeshes.resize(state.sectionBlockIndices.size());
    state.nativeLiquidSectionMeshes.resize(state.sectionBlockIndices.size());
    state.praxisCompatLiquidSections.resize(state.sectionBlockIndices.size());
    state.liquidProxySectionMeshes.resize(state.sectionBlockIndices.size());
    state.nativeLiquidSectionCellCounts.resize(state.sectionBlockIndices.size());
    state.liquidProxySectionCellCounts.resize(state.sectionBlockIndices.size());
    state.blockEntityPlaceholderSectionMeshes.resize(state.sectionBlockIndices.size());
    state.sectionExtraBlockPositions.resize(state.sectionBlockIndices.size());
    return resolveTerrainTexture(client, state);
}

void resetProjectionState(ProjectionState& state) {
    releaseProjectionState(state, ProjectionReleaseScope::World);
}

void suspendProjectionState(ProjectionState& state) {
    // A dimension owns its BlockSource, but the Level remains the identity of
    // the current world. Retaining only its listener preserves authoritative
    // world-exit cleanup while all dimension-owned runtime data is released.
    releaseProjectionState(state, ProjectionReleaseScope::Dimension);
}

ProjectionContextStatus classifyProjectionContext(
    ProjectionState const& state,
    IClientInstance&       client,
    Actor*                 player
) {
    // The local player can briefly disappear while a dimension is loading.
    // Level destruction remains the authoritative world-exit signal, so a
    // missing player pauses rendering instead of discarding the projection.
    if (!player) return ProjectionContextStatus::Unavailable;
    if (state.client != &client || state.level != &player->getLevel()) {
        return ProjectionContextStatus::WorldChanged;
    }
    if (state.dimension != &player->getDimension()) {
        return ProjectionContextStatus::DimensionChanged;
    }
    return ProjectionContextStatus::Current;
}

} // namespace lholo::projection::detail
