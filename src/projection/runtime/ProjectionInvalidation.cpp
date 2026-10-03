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

void markSectionDirty(ProjectionState& state, std::size_t section, bool incremental) {
    if (section >= state.sections.size()) return;
    auto& sectionState = state.sections[section];
    sectionState.dirty = true;
    sectionState.incrementalDirty = sectionState.incrementalDirty || incremental;
    ++sectionState.requestedRevision;
}

void markAllSectionsDirty(ProjectionState& state, bool incremental) {
    for (std::size_t section = 0; section < state.sections.size(); ++section) {
        markSectionDirty(state, section, incremental);
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
        || state.progressCorrect.size() < blockCount) {
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
    if (result.geometryTransformChanged || opacityChanged || correctionStyleChanged) {
        markAllSectionsDirty(state, false);
    }
    if (result.placementMoved && !result.geometryTransformChanged) {
        // Local geometry survives an XYZ move, but a task already sampling the
        // previous world position must not be accepted.
        for (auto& sectionState : state.sections) {
            ++sectionState.requestedRevision;
            if (sectionState.buildInFlight) sectionState.dirty = true;
        }
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
    if (result.layerChanged && !result.geometryTransformChanged) {
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
            if (oldLayerVisible(entry) == visible) continue;
            auto const section = state.blockToSection[index];
            if (section < state.sections.size()) {
                markSectionDirty(state, section, false);
            }
            state.correctionStates[index] = visible
                ? CorrectionState::Unknown
                : CorrectionState::Correct;
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

    if (result.placementViewChanged()) {
        state.detectedExtraBlockPositions.clear();
        state.extraBlockPositions.clear();
        for (auto& positions : state.sectionExtraBlockPositions) positions.clear();
        state.progressExtraCount = 0;
        // Error HUD counts follow the same visible range as the meshes.
        state.progressWrongTypeCount = state.progressWrongStateCount = 0;
        std::fill(state.progressErrorKind.begin(), state.progressErrorKind.end(), 0);
        state.extraScanRegion = 0;
        state.extraScanCell = 0;
        markAllSectionsDirty(state, false);
        publishErrorProgress(
            state.progressWrongTypeCount,
            state.progressWrongStateCount,
            0
        );
    }

    // Rotation/mirror alter local block models, so only those require throwing
    // away every GPU mesh. XYZ movement stays represented by the world matrix.
    if (result.geometryTransformChanged) {
        std::fill(
            state.correctionStates.begin(),
            state.correctionStates.end(),
            CorrectionState::Unknown
        );
        for (auto& sectionState : state.sections) {
            for (auto& mesh : sectionState.meshes) mesh.reset();
        }
        for (auto& mesh : state.warningFillSectionMeshes) mesh.reset();
        for (auto& mesh : state.correctionOutlineSectionMeshes) mesh.reset();
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
