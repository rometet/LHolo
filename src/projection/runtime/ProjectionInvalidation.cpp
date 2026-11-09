// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/runtime/ProjectionInvalidation.h"
#include "projection/core/ComparisonStyle.h"

#include "projection/core/ProjectionInternalTypes.h"
#include "projection/runtime/ProjectionProgress.h"
#include "projection/core/ProjectionRules.h"
#include "projection/core/ProjectionState.h"
#include "structure/StructureLoader.h"
#include "structure/StructureSession.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace lholo::projection::detail {
namespace {

void markSectionDirty(
    ProjectionState& state, std::size_t section, bool incremental, bool models = true
) {
    if (section >= state.sections.size()) return;
    auto& sectionState = state.sections[section];
    sectionState.modelDirty = sectionState.modelDirty || models;
    sectionState.dirty = true;
    sectionState.incrementalDirty = sectionState.incrementalDirty || incremental;
    ++sectionState.requestedRevision;
}

void markAllSectionsDirty(ProjectionState& state, bool incremental, bool models = true) {
    for (std::size_t section = 0; section < state.sections.size(); ++section) {
        markSectionDirty(state, section, incremental, models);
    }
}

} // namespace

ProjectionInvalidationResult reconcileProjectionInvalidation(
    ProjectionState&                      state,
    ProjectionInvalidationSettings const& settings
) {
    ProjectionInvalidationResult result;
    if (!state.structure) return result;

    auto const blockCount = state.structure->renderBlocks.size();
    if (state.blockToSection.size() < blockCount
        || state.correctionStates.size() < blockCount
        || state.progressCorrect.size() < blockCount
        || state.progressMaterialTypePresent.size() < blockCount) {
        return result;
    }

    result.geometryTransformChanged = state.cachedRotation != settings.rotationTurns
        || state.cachedMirror != settings.mirrorMode;
    result.placementMoved = state.cachedOffsetX != settings.offsetX
        || state.cachedOffsetY != settings.offsetY
        || state.cachedOffsetZ != settings.offsetZ;
    auto const countExtras = structure::detail::StructureSession::getInstance().countExtras();
    result.layerChanged = state.cachedCountExtras != countExtras || state.cachedLayerDisplayMode != settings.layerDisplayMode
        || state.cachedDisplayLayer != settings.displayLayer
        || state.cachedLayerAxis != settings.layerAxis;
    bool const opacityChanged
        = std::abs(state.cachedOpacity - settings.structureOpacity) > 0.0001f;
    bool const correctionStyleChanged
        = comparisonStyleChanged(
            {state.cachedCorrectionFillOpacity, state.cachedCorrectionOutlineOpacity,
             state.cachedComparisonStrength, state.cachedCorrectionOutlineWidth},
            {settings.correctionFillOpacity, settings.correctionOutlineOpacity,
             settings.comparisonStrength, settings.correctionOutlineWidth});
    if (!result.geometryTransformChanged && !result.placementMoved && !result.layerChanged
        && !opacityChanged && !correctionStyleChanged) {
        return result;
    }

    if (result.geometryTransformChanged || result.layerChanged
        || opacityChanged || correctionStyleChanged) {
        state.meshPreflightDone = false;
    }
    // A placement/layer/rotation change marks sections below, after its
    // correction/progress invalidation. Do not issue two revision bumps in the
    // same reconcile call; an XYZ move still requires fresh world corrections.
    if ((!result.placementViewChanged() || result.layerOnlyChanged())
        && (opacityChanged || correctionStyleChanged)) {
        // Opacity is baked into model vertex colors, so only it requires full
        // model rebuilds; correction-style changes rebuild overlays only.
        markAllSectionsDirty(state, false, opacityChanged);
    }

    auto const layerIsVisible = [&](structure::LoadedStructure::RenderBlock const& entry) {
        return isLayerVisible(
            projectionLayer(*state.structure, entry, settings.layerAxis, settings.mirrorMode, settings.rotationTurns),
            settings.layerDisplayMode,
            settings.displayLayer,
            entry.materialIndex,
            entry.liquidMaterialIndex,
            settings.layerAxis
        );
    };
    // LayerRange changes only invalidate sections containing blocks whose
    // visibility crossed the old/new boundary.
    if (result.layerChanged || result.geometryTransformChanged) {
        // A layer toggle changes the exposed correction faces of *adjacent*
        // sections too, even when none of their own cells changed visibility.
        // Coalesce section dirtiness: incrementing revisions once per cell
        // caused unnecessary rebuild churn for large schematics.
        std::vector<std::uint8_t> visibilityChangedSections;
        std::vector<std::uint8_t> newlyVisibleSections;
        if (!result.geometryTransformChanged) {
            visibilityChangedSections.resize(state.sections.size());
            newlyVisibleSections.resize(state.sections.size());
        }
        auto const oldLayerVisible = [&](structure::LoadedStructure::RenderBlock const& entry) {
            if (!state.cachedLayerDisplayMode || !state.cachedLayerAxis) return false;
            auto const layer = projectionLayer(*state.structure, entry, *state.cachedLayerAxis, state.cachedMirror, state.cachedRotation);
            return isLayerVisible(
                layer, *state.cachedLayerDisplayMode, state.cachedDisplayLayer,
                entry.materialIndex, entry.liquidMaterialIndex, *state.cachedLayerAxis
            );
        };
        for (std::size_t index = 0; index < state.structure->renderBlocks.size(); ++index) {
            auto const& entry = state.structure->renderBlocks[index];
            auto const visible = layerIsVisible(entry);
            // Rotation and initial placement also change which cells the paged
            // correction store must expose. Never leave its default all-visible
            // mask in place for a hidden layer.
            if (!result.geometryTransformChanged && oldLayerVisible(entry) == visible) continue;
            if (!result.geometryTransformChanged) {
                auto const section = state.correctionStates.section(index);
                if (section < visibilityChangedSections.size()) {
                    visibilityChangedSections[section] = 1;
                    if (visible) newlyVisibleSections[section] = 1;
                }
                // A cell hidden during initial placement was marked Correct
                // solely to suppress its ghost. If never scanned, restore
                // conservative Missing truth before exposing it to the mesh.
                if (visible && !state.initialCorrectionScan.visited(index)) {
                    state.correctionStates.set(index, CorrectionState::Missing);
                    state.missingLayers[index] = MissingLayerBoth;
                }
                auto const oldError = state.progressErrorKind[index];
                auto const truth = state.correctionStates.get(index);
                std::uint8_t const newError = !visible ? 0
                    : truth == CorrectionState::WrongType ? 1
                    : truth == CorrectionState::WrongState ? 2 : 0;
                if (oldError != newError) {
                    if (oldError == 1 && state.progressWrongTypeCount) --state.progressWrongTypeCount;
                    if (oldError == 2 && state.progressWrongStateCount) --state.progressWrongStateCount;
                    if (newError == 1) ++state.progressWrongTypeCount;
                    if (newError == 2) ++state.progressWrongStateCount;
                    state.progressErrorKind[index] = newError;
                }
            }
            state.correctionStates.setVisible(index, visible);
        }
        if (!result.geometryTransformChanged) {
            // Only changed sections need their model geometry regenerated.
            // The six face-neighbors need an overlay-only refresh: an exposed
            // top/side face can appear when the adjacent layer is hidden.
            constexpr int neighborOffsets[6][3] = {
                {-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
                {0, 1, 0}, {0, 0, -1}, {0, 0, 1}
            };
            std::vector<std::uint8_t> overlayNeighbors(state.sections.size());
            for (std::size_t section = 0; section < visibilityChangedSections.size(); ++section) {
                if (!visibilityChangedSections[section]) continue;
                markSectionDirty(state, section, false);
                state.sections[section].layerStale = true;
                if (section >= state.localSectionKeys.size()) continue;
                auto const [x, y, z] = state.localSectionKeys[section];
                for (auto const& delta : neighborOffsets) {
                    auto found = state.localSectionIndices.find(SubChunkKey{
                        x + delta[0], y + delta[1], z + delta[2]});
                    if (found != state.localSectionIndices.end()
                        && found->second < overlayNeighbors.size())
                        overlayNeighbors[found->second] = 1;
                }
            }
            for (std::size_t section = 0; section < overlayNeighbors.size(); ++section) {
                if (overlayNeighbors[section] && !visibilityChangedSections[section]) {
                    // Native model faces can be occluded by a neighbor's
                    // newly hidden block, not just the correction overlay.
                    markSectionDirty(state, section, false, true);
                    state.sections[section].layerStale = true;
                }
            }
            // Already-scanned hidden cells keep their real-world truth.
            // Only newly exposed sections need fresh comparisons; hiding a
            // layer is a pure render-mask change, not a new correction scan.
            state.correctionScan.revisitSections(state.sectionBlockIndices,
                newlyVisibleSections);
        }
    }

    state.cachedCountExtras = countExtras;
    state.cachedRotation = settings.rotationTurns;
    state.cachedMirror = settings.mirrorMode;
    state.cachedOffsetX = settings.offsetX;
    state.cachedOffsetY = settings.offsetY;
    state.cachedOffsetZ = settings.offsetZ;
    state.cachedLayerDisplayMode = settings.layerDisplayMode;
    state.cachedDisplayLayer = settings.displayLayer;
    state.cachedLayerAxis = settings.layerAxis;
    state.cachedOpacity = settings.structureOpacity;
    state.cachedCorrectionFillOpacity = settings.correctionFillOpacity;
    state.cachedCorrectionOutlineOpacity = settings.correctionOutlineOpacity;
    state.cachedComparisonStrength = settings.comparisonStrength;
    state.cachedCorrectionOutlineWidth = settings.correctionOutlineWidth;

    if (result.geometryTransformChanged || result.placementMoved) {
        state.detectedExtraBlockPositions.clear();
        state.detectedExtraSectionCounts.clear();
        state.extraBlockPositions.clear();
        for (auto& positions : state.sectionExtraBlockPositions) positions.clear();
        state.progressExtraCount = 0;
        // Error HUD counts follow the same visible range as the meshes.
        state.progressWrongTypeCount = state.progressWrongStateCount = 0;
        std::fill(state.progressErrorKind.begin(), state.progressErrorKind.end(), 0);
        state.extraScan.reset();
        markAllSectionsDirty(state, false);
        publishErrorProgress(
            state.progressWrongTypeCount,
            state.progressWrongStateCount,
            0
        );
    } else if (result.layerChanged) {
        // Extra world cells are layer-dependent. Invalidate their old overlay
        // sections, then rescan empty cells in the new visible range. Existing
        // block truth, GPU meshes in unaffected sections and native actors stay.
        for (std::size_t section = 0; section < state.sectionExtraBlockPositions.size(); ++section) {
            if (!state.sectionExtraBlockPositions[section].empty()) {
                markSectionDirty(state, section, false, false);
                state.sections[section].layerStale = true;
            }
            state.sectionExtraBlockPositions[section].clear();
        }
        state.detectedExtraBlockPositions.clear();
        state.detectedExtraSectionCounts.clear();
        state.extraBlockPositions.clear();
        state.progressExtraCount = 0;
        state.extraScan.reset();
        publishErrorProgress(state.progressWrongTypeCount,
            state.progressWrongStateCount, 0);
    }

    // Rotation/mirror alter local block models, so only those require throwing
    // away every GPU mesh. XYZ movement stays represented by the world matrix.
    if (result.geometryTransformChanged) {
        state.correctionStates.fill(CorrectionState::Missing);
        std::fill(state.missingLayers.begin(), state.missingLayers.end(), MissingLayerBoth);
        for (auto& sectionState : state.sections) {
            for (auto& mesh : sectionState.meshes) mesh.reset();
            sectionState.blendSource.reset();
            sectionState.blendSortKeyValid = false;
            sectionState.blendSortUnsupported = false;
        }
        for (auto& mesh : state.warningFillSectionMeshes) mesh.reset();
        for (auto& mesh : state.correctionOutlineSectionMeshes) mesh.reset();
        state.correctionDrawBatches.clear();
    state.transparentDrawBatches.clear();
        for (auto& mesh : state.wrongFillSectionMeshes) mesh.reset();
        for (auto& mesh : state.wrongOutlineSectionMeshes) mesh.reset();
        for (auto& mesh : state.nativeLiquidSectionMeshes) mesh.reset();
        for (auto& data : state.praxisCompatLiquidSections) data.reset();
        state.praxisCompatLiquidAggregate.reset();
        state.praxisCompatLiquidAggregateOrder.clear();
        state.praxisCompatLiquidBoundaryMaskCache.clear();
        state.praxisCompatLiquidAggregateDirty = true;
        for (auto& mesh : state.liquidProxySectionMeshes) mesh.reset();
        std::fill(
            state.nativeLiquidSectionCellCounts.begin(),
            state.nativeLiquidSectionCellCounts.end(),
            0
        );
        std::fill(
            state.liquidProxySectionCellCounts.begin(),
            state.liquidProxySectionCellCounts.end(),
            0
        );
        for (auto& mesh : state.blockEntityPlaceholderSectionMeshes) mesh.reset();
        state.structureBoundsMesh.reset();
        std::fill(state.progressCorrect.begin(), state.progressCorrect.end(), 0);
        std::fill(state.progressMaterialTypePresent.begin(),
            state.progressMaterialTypePresent.end(), 0);
        ++state.progressRevision;
        std::fill(state.progressErrorKind.begin(), state.progressErrorKind.end(), 0);
        state.progressCorrectCount = 0;
        state.progressVisibleCorrectCount = 0;
        state.progressWrongTypeCount = 0;
        state.progressWrongStateCount = 0;
        state.progressExtraCount = 0;
        resetPublishedBuildProgressCounts();
    }

    if (result.geometryTransformChanged || result.layerChanged) {
        state.progressVisibleCorrectCount = 0;
        std::uint64_t visibleTotal{};
        for (std::size_t index = 0; index < state.structure->renderBlocks.size(); ++index) {
            auto const& entry = state.structure->renderBlocks[index];
            if (!layerIsVisible(entry)) continue;
            ++visibleTotal;
            if (state.progressCorrect[index] != 0) {
                ++state.progressVisibleCorrectCount;
            }
        }
        publishVisibleProgress(state.progressVisibleCorrectCount, visibleTotal);
    }

    return result;
}

} // namespace lholo::projection::detail
