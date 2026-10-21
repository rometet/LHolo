// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#include "projection/correction/ProjectionCorrectionTracker.h"
#include "projection/correction/ExtraCellRegistration.h"
#include "app/NativeCallbackBoundary.h"
#include "projection/core/ProjectionCoordinateBounds.h"

#include "block/BlockPlacementRules.h"
#include "projection/core/ProjectionRules.h"
#include "projection/core/ProjectionState.h"
#include "projection/runtime/ProjectionWorldEvents.h"
#include "structure/StructureLoader.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <tuple>

#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "structure/Verification.h"
#include "structure/StructureSession.h"
#include "mc/world/level/levelgen/structure/LegacyStructureSettings.h"

namespace lholo::projection::detail {
namespace {

void markSectionDirty(ProjectionState& state, std::size_t section) {
    if (section >= state.sections.size()) return;
    auto& sectionState = state.sections[section];
    sectionState.dirty = true;
    sectionState.incrementalDirty = true;
    ++sectionState.requestedRevision;
}

SubChunkKey localSectionKey(BlockPos const& position) {
    auto const floorDiv16 = [](int value) {
        return value >= 0 ? value / 16 : -1 - ((-1 - value) / 16);
    };
    return {floorDiv16(position.x), floorDiv16(position.y), floorDiv16(position.z)};
}

constexpr std::size_t kInvalidSection = std::numeric_limits<std::size_t>::max();

bool sectionStorageAligned(ProjectionState const& state) {
    auto const count = state.sections.size();
    return state.localSectionKeys.size() == count
        && state.sectionBlockIndices.size() == count
        && state.sectionExtraBlockPositions.size() == count
        && state.warningFillSectionMeshes.size() == count
        && state.correctionOutlineSectionMeshes.size() == count
        && state.wrongFillSectionMeshes.size() == count
        && state.wrongOutlineSectionMeshes.size() == count
        && state.nativeLiquidSectionMeshes.size() == count
        && state.praxisCompatLiquidSections.size() == count
        && state.liquidProxySectionMeshes.size() == count
        && state.nativeLiquidSectionCellCounts.size() == count
        && state.liquidProxySectionCellCounts.size() == count
        && state.blockEntityPlaceholderSectionMeshes.size() == count;
}

bool sectionStorageContains(ProjectionState const& state, std::size_t section) {
    return section < state.sections.size()
        && section < state.localSectionKeys.size()
        && section < state.sectionBlockIndices.size()
        && section < state.sectionExtraBlockPositions.size()
        && section < state.warningFillSectionMeshes.size()
        && section < state.correctionOutlineSectionMeshes.size()
        && section < state.wrongFillSectionMeshes.size()
        && section < state.wrongOutlineSectionMeshes.size()
        && section < state.nativeLiquidSectionMeshes.size()
        && section < state.praxisCompatLiquidSections.size()
        && section < state.liquidProxySectionMeshes.size()
        && section < state.nativeLiquidSectionCellCounts.size()
        && section < state.liquidProxySectionCellCounts.size()
        && section < state.blockEntityPlaceholderSectionMeshes.size();
}

std::size_t ensureCorrectionSection(
    ProjectionState& state,
    BlockPos const&  position,
    BlockPos const&  transformedPosition
) {
    auto const key = localSectionKey(position);
    if (auto const found = state.localSectionIndices.find(key);
        found != state.localSectionIndices.end()) {
        auto const section = found->second;
        if (!sectionStorageContains(state, section)) return kInvalidSection;
        if (state.sectionBlockIndices[section].empty()) {
            state.sections[section].center = Vec3{
                static_cast<float>(transformedPosition.x) + 0.5f,
                static_cast<float>(transformedPosition.y) + 0.5f,
                static_cast<float>(transformedPosition.z) + 0.5f,
            };
        }
        return section;
    }

    // All parallel section stores are an ABI of their own. If one is already
    // out of sync, do not extend or index any of them.
    if (!sectionStorageAligned(state)) return kInvalidSection;

    auto const section = state.sections.size();
    auto const targetSize = section + 1;
    try {
        // Reserve every vector before changing any size. A reserve failure can
        // change capacity but leaves all logical sizes aligned.
        state.sections.reserve(targetSize);
        state.localSectionKeys.reserve(targetSize);
        state.sectionBlockIndices.reserve(targetSize);
        state.sectionExtraBlockPositions.reserve(targetSize);
        state.warningFillSectionMeshes.reserve(targetSize);
        state.correctionOutlineSectionMeshes.reserve(targetSize);
        state.wrongFillSectionMeshes.reserve(targetSize);
        state.wrongOutlineSectionMeshes.reserve(targetSize);
        state.nativeLiquidSectionMeshes.reserve(targetSize);
        state.praxisCompatLiquidSections.reserve(targetSize);
        state.liquidProxySectionMeshes.reserve(targetSize);
        state.nativeLiquidSectionCellCounts.reserve(targetSize);
        state.liquidProxySectionCellCounts.reserve(targetSize);
        state.blockEntityPlaceholderSectionMeshes.reserve(targetSize);

        SectionState sectionState;
        sectionState.center = Vec3{
            static_cast<float>(transformedPosition.x) + 0.5f,
            static_cast<float>(transformedPosition.y) + 0.5f,
            static_cast<float>(transformedPosition.z) + 0.5f,
        };
        sectionState.dirty = true;
        sectionState.requestedRevision = 1;

        state.sections.push_back(std::move(sectionState));
        state.localSectionKeys.push_back(key);
        state.sectionBlockIndices.emplace_back();
        state.sectionExtraBlockPositions.emplace_back();
        state.warningFillSectionMeshes.emplace_back();
        state.correctionOutlineSectionMeshes.emplace_back();
        state.wrongFillSectionMeshes.emplace_back();
        state.wrongOutlineSectionMeshes.emplace_back();
        state.nativeLiquidSectionMeshes.emplace_back();
        state.praxisCompatLiquidSections.emplace_back();
        state.liquidProxySectionMeshes.emplace_back();
        state.nativeLiquidSectionCellCounts.emplace_back();
        state.liquidProxySectionCellCounts.emplace_back();
        state.blockEntityPlaceholderSectionMeshes.emplace_back();

        auto const [found, inserted] = state.localSectionIndices.emplace(key, section);
        if (inserted) return section;

        // Defensive only: this function is render-thread owned, but if an
        // unexpected duplicate appears, roll back the newly appended stores.
        state.sections.resize(section);
        state.localSectionKeys.resize(section);
        state.sectionBlockIndices.resize(section);
        state.sectionExtraBlockPositions.resize(section);
        state.warningFillSectionMeshes.resize(section);
        state.correctionOutlineSectionMeshes.resize(section);
        state.wrongFillSectionMeshes.resize(section);
        state.wrongOutlineSectionMeshes.resize(section);
        state.nativeLiquidSectionMeshes.resize(section);
        state.praxisCompatLiquidSections.resize(section);
        state.liquidProxySectionMeshes.resize(section);
        state.nativeLiquidSectionCellCounts.resize(section);
        state.liquidProxySectionCellCounts.resize(section);
        state.blockEntityPlaceholderSectionMeshes.resize(section);
        return sectionStorageContains(state, found->second)
            ? found->second : kInvalidSection;
    } catch (...) {
        // Any append after successful reserves is expected not to allocate, but
        // keep a rollback barrier anyway so a future type change cannot leave
        // the parallel stores at different sizes.
        state.sections.resize(std::min(section, state.sections.size()));
        state.localSectionKeys.resize(std::min(section, state.localSectionKeys.size()));
        state.sectionBlockIndices.resize(std::min(section, state.sectionBlockIndices.size()));
        state.sectionExtraBlockPositions.resize(
            std::min(section, state.sectionExtraBlockPositions.size())
        );
        state.warningFillSectionMeshes.resize(
            std::min(section, state.warningFillSectionMeshes.size())
        );
        state.correctionOutlineSectionMeshes.resize(
            std::min(section, state.correctionOutlineSectionMeshes.size())
        );
        state.wrongFillSectionMeshes.resize(
            std::min(section, state.wrongFillSectionMeshes.size())
        );
        state.wrongOutlineSectionMeshes.resize(
            std::min(section, state.wrongOutlineSectionMeshes.size())
        );
        state.nativeLiquidSectionMeshes.resize(
            std::min(section, state.nativeLiquidSectionMeshes.size())
        );
        state.praxisCompatLiquidSections.resize(
            std::min(section, state.praxisCompatLiquidSections.size())
        );
        state.liquidProxySectionMeshes.resize(
            std::min(section, state.liquidProxySectionMeshes.size())
        );
        state.nativeLiquidSectionCellCounts.resize(
            std::min(section, state.nativeLiquidSectionCellCounts.size())
        );
        state.liquidProxySectionCellCounts.resize(
            std::min(section, state.liquidProxySectionCellCounts.size())
        );
        state.blockEntityPlaceholderSectionMeshes.resize(
            std::min(section, state.blockEntityPlaceholderSectionMeshes.size())
        );
        app::reportNativeCallbackFailure("correction section creation", "section append failed; parallel storage rolled back");
        // The caller has already advanced the extra-cell scan and may have
        // published its HUD count. Returning an invalid section would leave
        // this cell permanently without geometry in a stable world. Let the
        // correction boundary invalidate the partial epoch instead.
        throw;
    }
}

} // namespace

CorrectionProgressChanges updateCorrectionTracker(
    ProjectionState&                state,
    BlockSource&                    region,
    LegacyStructureSettings const& transformSettings,
    int                             mirrorMode,
    int                             rotationTurns,
    int                             offsetX,
    int                             offsetY,
    int                             offsetZ,
    structure::LayerDisplayMode     layerDisplayMode,
    int                             displayLayer,
    structure::LayerAxis            layerAxis
) {
    CorrectionProgressChanges changes;
    if (!state.structure || !state.expectedWorldBlockIndices) return changes;

    auto const totalBlocks = state.structure->renderBlocks.size();
    // All per-block arrays must be index-aligned with renderBlocks. If a prior
    // allocation or lifecycle fault broke that invariant, skip correction
    // work rather than turning it into an out-of-bounds access.
    if (state.missingLayers.size() < totalBlocks
        || state.correctionStates.size() < totalBlocks
        || state.progressCorrect.size() < totalBlocks
        || state.progressErrorKind.size() < totalBlocks
        || state.blockToSection.size() < totalBlocks) {
        return changes;
    }
    if (state.correctionScanCursor > totalBlocks) {
        state.correctionScanCursor = totalBlocks;
    }

    // Share one fixed world-read budget between initial cache population and
    // incremental block notifications.
    constexpr std::size_t kCorrectionChecksPerFrame = 4096;
    constexpr std::size_t kSubChunkEventsPerFrame    = 64;
    bool const identityTransform = mirrorMode == 0 && rotationTurns == 0;
    bool const countExtras = structure::detail::StructureSession::getInstance().countExtras();

    auto const updateCorrection = [&](std::size_t index) {
        if (index >= totalBlocks) return;
        auto const& entry = state.structure->renderBlocks[index];
        auto const visible = isLayerVisible(
            projectionLayer(*state.structure, entry, layerAxis, mirrorMode, rotationTurns),
            layerDisplayMode, displayLayer,
            entry.materialIndex, entry.liquidMaterialIndex, layerAxis
        );
        auto const transformed = transformStructurePosition(
            entry, *state.structure, mirrorMode, rotationTurns
        );
        BlockPos const position{
            state.anchor.x + offsetX + transformed.x,
            state.anchor.y + offsetY + transformed.y,
            state.anchor.z + offsetZ + transformed.z
        };
        auto const* expected = transformExpectedBlock(entry.block, transformSettings, identityTransform);
        auto const* expectedLiquid = transformExpectedBlock(
            entry.liquid, transformSettings, identityTransform
        );
        auto const& actual = region.getBlock(position);
        auto const isBubbleColumn = (expected && expected->getTypeName() == "minecraft:bubble_column")
            || (actual.getTypeName() == "minecraft:bubble_column");
        auto const& actualLiquid = isBubbleColumn
            ? region.getExtraBlock(position)
            : region.getLiquidBlock(position);
        auto const bodyMissing = expected && actual.isAir();
        auto const liquidMissing = expectedLiquid && actualLiquid.isAir();
        auto const bodyTypeWrong = expected
            && !actual.isAir()
            && block::placeableBaseName(actual.getTypeName())
                != block::placeableBaseName(expected->getTypeName());
        auto const liquidTypeWrong = expectedLiquid
            && !actualLiquid.isAir() && actualLiquid.getTypeName() != expectedLiquid->getTypeName();
        auto const liquidCellOccupiedBySolid = !expected && expectedLiquid && !actual.isAir()
            && actual.getTypeName() != expectedLiquid->getTypeName();
        bool const ready = region.areChunksFullyLoaded(position, 0)
            && !structure::placeholderBlock(actual.getTypeName())
            && (!expectedLiquid || !structure::placeholderBlock(actualLiquid.getTypeName()))
            && (!expected || !structure::placeholderBlock(expected->getTypeName()))
            && (!expectedLiquid || !structure::placeholderBlock(expectedLiquid->getTypeName()));
        auto nextState = CorrectionState::Correct;
        if (!ready) {
            nextState = CorrectionState::Unknown;
        } else if (bodyMissing || liquidMissing) {
            nextState = CorrectionState::Missing;
        } else if (bodyTypeWrong || liquidTypeWrong || liquidCellOccupiedBySolid) {
            nextState = CorrectionState::WrongType;
        } else if ((expected && !projectionStatesMatch(
                        // Real worlds maintain flattened fence connection
                        // booleans on block updates while structure palettes
                        // never store them, so a correctly built fence only
                        // matches its block recomputed for this neighborhood.
                        withFlattenedConnections(*expected, region, position),
                        actual
                    ))
            || (expectedLiquid && !projectionStatesMatch(*expectedLiquid, actualLiquid))) {
            nextState = CorrectionState::WrongState;
        }
        auto const nextMissingLayers = ready
            ? projectionMissingLayerMask(bodyMissing, liquidMissing) : MissingLayerBoth;
        auto const nowCorrect = nextState == CorrectionState::Correct;
        auto const wasCorrect = state.progressCorrect[index] != 0;
        if (nowCorrect != wasCorrect) {
            state.progressCorrect[index] = nowCorrect ? 1 : 0;
            ++state.progressRevision;
            if (nowCorrect) {
                ++state.progressCorrectCount;
            } else if (state.progressCorrectCount != 0) {
                --state.progressCorrectCount;
            }
            changes.overall = true;
            if (visible) {
                if (nowCorrect) {
                    ++state.progressVisibleCorrectCount;
                } else if (state.progressVisibleCorrectCount != 0) {
                    --state.progressVisibleCorrectCount;
                }
                changes.visible = true;
            }
        }
        auto const nextErrorKind = !visible ? uchar{0} : nextState == CorrectionState::WrongType ? uchar{1}
            : nextState == CorrectionState::WrongState ? uchar{2}
            : uchar{0};
        auto const previousErrorKind = state.progressErrorKind[index];
        if (nextErrorKind != previousErrorKind) {
            if (previousErrorKind == 1) {
                if (state.progressWrongTypeCount != 0) --state.progressWrongTypeCount;
            } else if (previousErrorKind == 2) {
                if (state.progressWrongStateCount != 0) --state.progressWrongStateCount;
            }
            if (nextErrorKind == 1) ++state.progressWrongTypeCount;
            else if (nextErrorKind == 2) ++state.progressWrongStateCount;
            state.progressErrorKind[index] = nextErrorKind;
            changes.errors = true;
        }
        // Retain whole-structure correctness for legacy progress consumers;
        // mistake counts and correction meshes follow the visible range.
        if (!visible) return;
        if (state.correctionStates[index] != nextState
            || state.missingLayers[index] != nextMissingLayers) {
            state.correctionStates[index] = nextState;
            state.missingLayers[index] = nextMissingLayers;
            auto const section = state.blockToSection[index];
            if (sectionStorageContains(state, section)) {
                markSectionDirty(state, section);
            }
            // A missing-cell shell omits faces shared with adjacent missing
            // cells. If either side changes, both section meshes may need an
            // exposed face added or removed (including across 16^3 borders).
            constexpr int neighbors[6][3] = {
                {-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
                {0, 1, 0}, {0, 0, -1}, {0, 0, 1}
            };
            for (auto const& delta : neighbors) {
                auto const neighbor = state.expectedWorldBlockIndices->find(std::tuple{
                    position.x + delta[0], position.y + delta[1], position.z + delta[2]
                });
                if (neighbor != state.expectedWorldBlockIndices->end()
                    && neighbor->second < state.blockToSection.size()) {
                    auto const neighborSection = state.blockToSection[neighbor->second];
                    if (sectionStorageContains(state, neighborSection)) {
                        markSectionDirty(state, neighborSection);
                    }
                }
            }
        }
    };

    auto const hasExpectedLocalCell = [&](BlockPos const& localPosition) {
        auto const found = std::lower_bound(
            state.structure->renderBlocks.begin(),
            state.structure->renderBlocks.end(),
            localPosition,
            [](structure::LoadedStructure::RenderBlock const& entry, BlockPos const& position) {
                return std::tie(entry.x, entry.y, entry.z)
                    < std::tie(position.x, position.y, position.z);
            }
        );
        return found != state.structure->renderBlocks.end()
            && found->x == localPosition.x
            && found->y == localPosition.y
            && found->z == localPosition.z;
    };

    auto const updateExtra = [&](
        BlockPos const& localPosition,
        BlockPos const& worldPosition,
        bool            visible
    ) {
        auto const key = SubChunkKey{localPosition.x, localPosition.y, localPosition.z};
        auto const& actual = region.getBlock(worldPosition);
        bool const isExtra = countExtras && visible
            && region.areChunksFullyLoaded(worldPosition, 0) && !actual.isAir()
            && !structure::placeholderBlock(actual.getTypeName());
        auto const detected = state.detectedExtraBlockPositions.find(key);
        if (isExtra != (detected != state.detectedExtraBlockPositions.end())) {
            if (isExtra) {
                state.detectedExtraBlockPositions.insert(key);
                ++state.progressExtraCount;
            } else {
                state.detectedExtraBlockPositions.erase(detected);
                if (state.progressExtraCount != 0) --state.progressExtraCount;
            }
            changes.errors = true;
        }

        bool const shouldRender = isExtra && visible;
        auto const rendered = state.extraBlockPositions.find(key);
        if (shouldRender == (rendered != state.extraBlockPositions.end())) return;

        std::size_t section{};
        if (shouldRender) {
            auto const transformedPosition = transformStructurePosition(
                localPosition, *state.structure, mirrorMode, rotationTurns
            );
            section = ensureCorrectionSection(state, localPosition, transformedPosition);
            if (section == kInvalidSection || !sectionStorageContains(state, section)) return;
            registerExtraCell(state.extraBlockPositions, state.sectionExtraBlockPositions[section], key);
        } else {
            auto const sectionFound = state.localSectionIndices.find(localSectionKey(localPosition));
            if (sectionFound == state.localSectionIndices.end()) return;
            section = sectionFound->second;
            if (!sectionStorageContains(state, section)) return;
            state.extraBlockPositions.erase(rendered);
            state.sectionExtraBlockPositions[section].erase(key);
        }
        markSectionDirty(state, section);
        constexpr int neighbors[6][3] = {
            {-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
            {0, 1, 0}, {0, 0, -1}, {0, 0, 1}
        };
        for (auto const& delta : neighbors) {
            BlockPos const neighbor{
                localPosition.x + delta[0],
                localPosition.y + delta[1],
                localPosition.z + delta[2],
            };
            auto const found = state.localSectionIndices.find(localSectionKey(neighbor));
            if (found != state.localSectionIndices.end()
                && sectionStorageContains(state, found->second)) {
                markSectionDirty(state, found->second);
            }
        }
    };

    // The cache is populated once after a structure/transform change. Once
    // that pass completes, a stable world performs no correction reads.
    // Official BlockSource notifications update only changed cells.
    auto const changedPositions = takePendingBlockChanges(kCorrectionChecksPerFrame);
    std::size_t correctionChecks{};
    for (auto const& change : changedPositions) {
        auto const& changedPosition = change.position;
        auto const found = state.expectedWorldBlockIndices->find(std::tuple{
            changedPosition.x, changedPosition.y, changedPosition.z
        });
        if (found != state.expectedWorldBlockIndices->end()) {
            if (change.destroyedAt != 0) {
                state.pendingBrokenCells.push_back(BrokenProjectionCell{
                    changedPosition.x,
                    changedPosition.y,
                    changedPosition.z,
                    change.destroyedAt,
                });
            }
            updateCorrection(found->second);
            ++correctionChecks;
            continue;
        }
        auto const relative = checkedRelativeBlockCell(
            {changedPosition.x, changedPosition.y, changedPosition.z},
            {state.anchor.x, state.anchor.y, state.anchor.z}, {offsetX, offsetY, offsetZ});
        if (!relative) continue; // A distant event cannot belong to the validated projection volume.
        BlockPos const transformed{(*relative)[0], (*relative)[1], (*relative)[2]};
        auto const local = inverseTransformStructurePosition(
            transformed, *state.structure, mirrorMode, rotationTurns
        );
        if (isStructureCellCovered(*state.structure, local)
            && !hasExpectedLocalCell(local)) {
            auto const visible = isLayerVisible(
                projectionLayer(*state.structure, local, layerAxis, mirrorMode, rotationTurns),
                layerDisplayMode,
                displayLayer,
                -1,
                -1,
                layerAxis
            );
            updateExtra(local, changedPosition, visible);
            ++correctionChecks;
        }
    }

    auto const loadedSubChunks = takePendingLoadedSubChunks(kSubChunkEventsPerFrame);
    state.pendingLoadedSubChunks.insert(loadedSubChunks.begin(), loadedSubChunks.end());

    auto const scanRemaining = totalBlocks - state.correctionScanCursor;
    auto const remainingBudget = correctionChecks >= kCorrectionChecksPerFrame
        ? std::size_t{0}
        : kCorrectionChecksPerFrame - correctionChecks;
    auto const checks = std::min(scanRemaining, remainingBudget);
    for (std::size_t checked = 0; checked < checks; ++checked) {
        updateCorrection(state.correctionScanCursor++);
    }
    correctionChecks += checks;

    // Air cells have no render-block index, so discover extras with a separate
    // cursor while sharing the same fixed per-frame correction budget. Region
    // boxes preserve litematic gaps and avoid scanning their merged bounds.
    while (correctionChecks < kCorrectionChecksPerFrame
        && state.extraScanRegion < state.structure->regions.size()) {
        auto const& box = state.structure->regions[state.extraScanRegion];
        if (box.sizeX <= 0 || box.sizeY <= 0 || box.sizeZ <= 0) {
            ++state.extraScanRegion;
            state.extraScanCell = 0;
            continue;
        }
        auto const regionVolume = static_cast<std::uint64_t>(box.sizeX)
            * static_cast<std::uint64_t>(box.sizeY) * static_cast<std::uint64_t>(box.sizeZ);
        if (state.extraScanCell >= regionVolume) {
            ++state.extraScanRegion;
            state.extraScanCell = 0;
            continue;
        }
        auto const yz = static_cast<std::uint64_t>(box.sizeY) * box.sizeZ;
        auto const x = state.extraScanCell / yz;
        auto const remainder = state.extraScanCell % yz;
        auto const y = remainder / static_cast<std::uint64_t>(box.sizeZ);
        auto const z = remainder % static_cast<std::uint64_t>(box.sizeZ);
        ++state.extraScanCell;
        ++correctionChecks;
        BlockPos const local{
            box.x + static_cast<int>(x),
            box.y + static_cast<int>(y),
            box.z + static_cast<int>(z),
        };
        auto const visible = isLayerVisible(
            projectionLayer(*state.structure, local, layerAxis, mirrorMode, rotationTurns),
            layerDisplayMode,
            displayLayer,
            -1,
            -1,
            layerAxis
        );
        if (hasExpectedLocalCell(local)) continue;
        auto const transformed = transformStructurePosition(
            local, *state.structure, mirrorMode, rotationTurns
        );
        BlockPos const world{
            state.anchor.x + offsetX + transformed.x,
            state.anchor.y + offsetY + transformed.y,
            state.anchor.z + offsetZ + transformed.z,
        };
        updateExtra(local, world, visible);
    }

    // A newly received client subchunk may not emit one block notification per
    // cell. Refresh only its projected cells, capped to one 16^3 region/frame.
    if (state.correctionScanCursor == totalBlocks
        && state.extraScanRegion >= state.structure->regions.size()
        && correctionChecks == 0
        && !state.pendingLoadedSubChunks.empty()) {
        auto loaded = state.pendingLoadedSubChunks.begin();
        auto const [subChunkX, subChunkY, subChunkZ] = *loaded;
        state.pendingLoadedSubChunks.erase(loaded);
        auto const bounds = checkedSubChunkBlockBounds({subChunkX, subChunkY, subChunkZ});
        if (!bounds) return changes;
        for (auto wideX = bounds->min[0]; wideX < bounds->max[0]; ++wideX) {
            for (auto wideY = bounds->min[1]; wideY < bounds->max[1]; ++wideY) {
                for (auto wideZ = bounds->min[2]; wideZ < bounds->max[2]; ++wideZ) {
                    auto const x = static_cast<int>(wideX);
                    auto const y = static_cast<int>(wideY);
                    auto const z = static_cast<int>(wideZ);
                    auto const found = state.expectedWorldBlockIndices->find(std::tuple{x, y, z});
                    if (found != state.expectedWorldBlockIndices->end()) {
                        updateCorrection(found->second);
                        continue;
                    }
                    auto const relative = checkedRelativeBlockCell({x, y, z},
                        {state.anchor.x, state.anchor.y, state.anchor.z}, {offsetX, offsetY, offsetZ});
                    if (!relative) continue;
                    BlockPos const transformed{(*relative)[0], (*relative)[1], (*relative)[2]};
                    auto const local = inverseTransformStructurePosition(
                        transformed, *state.structure, mirrorMode, rotationTurns
                    );
                    if (!isStructureCellCovered(*state.structure, local)
                        || hasExpectedLocalCell(local)) {
                        continue;
                    }
                    auto const visible = isLayerVisible(
                        projectionLayer(*state.structure, local, layerAxis, mirrorMode, rotationTurns),
                        layerDisplayMode,
                        displayLayer,
                        -1,
                        -1,
                        layerAxis
                    );
                    updateExtra(local, BlockPos{x, y, z}, visible);
                }
            }
        }
    }
    return changes;
}

} // namespace lholo::projection::detail
