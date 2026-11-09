// Retain uploaded GPU geometry across native subregion visibility edits.
// This code is render-owner-only. Workers are drained before capture and
// mesh ownership never crosses a world/dimension/resource-pack transition.
#include "projection/runtime/ProjectionRegionMeshReuse.h"
#include "projection/core/ProjectionState.h"
#include "projection/core/ProjectionRules.h"
#include "projection/correction/CorrectionCellClassification.h"
#include "projection/runtime/ProjectionRegionReusePolicy.h"
#include "structure/StructureLoader.h"

#include <algorithm>
#include <array>
#include <set>
#include <tuple>
#include <utility>

namespace lholo::projection::detail {
namespace {

using RenderBlock=structure::LoadedStructure::RenderBlock;

bool sameCell(RenderBlock const& a, RenderBlock const& b,
    bool compareMaterialLayers) noexcept {
    return a.x==b.x && a.y==b.y && a.z==b.z &&
        a.block==b.block && a.liquid==b.liquid &&
        a.blockEntityNbt==b.blockEntityNbt &&
        (!compareMaterialLayers ||
         (a.materialIndex==b.materialIndex &&
          a.liquidMaterialIndex==b.liquidMaterialIndex));
}

// Source-section delta detection lives in the shared tested region policy.

bool sameSettings(RegionMeshRetention const& old,ProjectionState const& now) noexcept {
    return old.rotation==now.cachedRotation && old.mirror==now.cachedMirror
        && old.countExtras==now.cachedCountExtras
        && old.layerMode==now.cachedLayerDisplayMode
        && old.layerAxis==now.cachedLayerAxis
        && old.displayLayer==now.cachedDisplayLayer
        && old.opacity==now.cachedOpacity
        && old.fillOpacity==now.cachedCorrectionFillOpacity
        && old.outlineOpacity==now.cachedCorrectionOutlineOpacity
        && old.comparisonStrength==now.cachedComparisonStrength
        && old.outlineWidth==now.cachedCorrectionOutlineWidth
        && static_cast<std::int64_t>(old.anchor.x)+old.offsetX==
           static_cast<std::int64_t>(now.anchor.x)+now.cachedOffsetX
        && static_cast<std::int64_t>(old.anchor.y)+old.offsetY==
           static_cast<std::int64_t>(now.anchor.y)+now.cachedOffsetY
        && static_cast<std::int64_t>(old.anchor.z)+old.offsetZ==
           static_cast<std::int64_t>(now.anchor.z)+now.cachedOffsetZ;
}

bool compatibleSources(ProjectionState const& old,
    std::shared_ptr<structure::LoadedStructure const> const& next) noexcept {
    return old.enabled && old.structure && next
        && old.structure->sourceBacking && next->sourceBacking
        && old.structure->sourceBacking==next->sourceBacking
        && old.structure->sizeX==next->sizeX
        && old.structure->sizeY==next->sizeY
        && old.structure->sizeZ==next->sizeZ
        && old.initialCorrectionScan.complete()
        && !old.placementBuildActive && !old.placementCoordinatesInvalid
        && old.cachedRotation>=0 && old.cachedMirror>=0
        && old.sectionBlockIndices.size()==old.sections.size();
}

} // namespace

std::unique_ptr<RegionMeshRetention> captureRegionMeshReuse(
    ProjectionState& old,
    std::shared_ptr<structure::LoadedStructure const> const& next) {
    if(!compatibleSources(old,next))return {};
    auto saved=std::make_unique<RegionMeshRetention>();
    saved->source=old.structure;
    saved->sectionMap=std::move(old.localSectionIndices);
    saved->sectionBlockIndices=std::move(old.sectionBlockIndices);
    saved->sectionExtraBlocks=std::move(old.sectionExtraBlockPositions);
    saved->sections=std::move(old.sections);
    saved->corrections=std::move(old.correctionStates);
    saved->missingLayers=std::move(old.missingLayers);
    saved->actorRendererAvailable=std::move(old.blockActorRendererAvailable);
    saved->warningFill=std::move(old.warningFillSectionMeshes);
    saved->correctionOutline=std::move(old.correctionOutlineSectionMeshes);
    saved->wrongFill=std::move(old.wrongFillSectionMeshes);
    saved->wrongOutline=std::move(old.wrongOutlineSectionMeshes);
    saved->nativeLiquid=std::move(old.nativeLiquidSectionMeshes);
    saved->liquidProxy=std::move(old.liquidProxySectionMeshes);
    saved->blockEntityPlaceholder=std::move(old.blockEntityPlaceholderSectionMeshes);
    saved->praxisLiquid=std::move(old.praxisCompatLiquidSections);
    saved->nativeLiquidCounts=std::move(old.nativeLiquidSectionCellCounts);
    saved->liquidProxyCounts=std::move(old.liquidProxySectionCellCounts);
    saved->anchor=old.anchor;
    saved->rotation=old.cachedRotation;
    saved->mirror=old.cachedMirror;
    saved->offsetX=old.cachedOffsetX;
    saved->offsetY=old.cachedOffsetY;
    saved->offsetZ=old.cachedOffsetZ;
    saved->countExtras=old.cachedCountExtras;
    saved->layerMode=old.cachedLayerDisplayMode;
    saved->layerAxis=old.cachedLayerAxis;
    saved->displayLayer=old.cachedDisplayLayer;
    saved->opacity=old.cachedOpacity;
    saved->fillOpacity=old.cachedCorrectionFillOpacity;
    saved->outlineOpacity=old.cachedCorrectionOutlineOpacity;
    saved->comparisonStrength=old.cachedComparisonStrength;
    saved->outlineWidth=old.cachedCorrectionOutlineWidth;
    return saved;
}

std::size_t restoreUnchangedRegionMeshes(ProjectionState& now) noexcept {
    if(!now.pendingRegionReuse)return 0;
    auto saved=std::move(now.pendingRegionReuse);
    try {
        if(!now.structure || !saved->source ||
           !now.structure->sourceBacking ||
           now.structure->sourceBacking!=saved->source->sourceBacking ||
           !sameSettings(*saved,now) ||
           now.placementBuildActive || now.placementCoordinatesInvalid)return 0;
        auto const compareMaterials=now.cachedLayerAxis==structure::LayerAxis::Material;
        auto const touched=changedRegionSectionHalo(
            saved->source->renderBlocks,now.structure->renderBlocks,
            [&](RenderBlock const& a,RenderBlock const& b){
                return sameCell(a,b,compareMaterials);
            });
        std::size_t reused{};
        for(std::size_t newSection=0;newSection<now.localSectionKeys.size();++newSection) {
            auto const& key=now.localSectionKeys[newSection];
            if(touched.contains(key))continue;
            auto const entry=saved->sectionMap.find(key);
            if(entry==saved->sectionMap.end())continue;
            auto const oldSection=entry->second;
            if(oldSection>=saved->sections.size() ||
               oldSection>=saved->sectionBlockIndices.size() ||
               oldSection>=saved->sectionExtraBlocks.size() ||
               newSection>=now.sections.size() ||
               newSection>=now.sectionBlockIndices.size())continue;
            auto& before=saved->sections[oldSection];
            auto& after=now.sections[newSection];
            if(before.dirty || before.buildInFlight ||
               before.uploadedRevision==0 ||
               !saved->sectionExtraBlocks[oldSection].empty())continue;
            auto const& beforeIndices=saved->sectionBlockIndices[oldSection];
            auto const& afterIndices=now.sectionBlockIndices[newSection];
            if(beforeIndices.size()!=afterIndices.size())continue;
            bool same=true;
            for(std::size_t i=0;i<beforeIndices.size();++i){
                auto const previous=beforeIndices[i],current=afterIndices[i];
                if(previous>=saved->source->renderBlocks.size() ||
                   current>=now.structure->renderBlocks.size() ||
                   previous>=saved->corrections.size() ||
                   previous>=saved->missingLayers.size() ||
                   previous>=saved->actorRendererAvailable.size() ||
                   current>=now.correctionStates.size() ||
                   current>=now.missingLayers.size() ||
                   current>=now.blockActorRendererAvailable.size() ||
                   !sameCell(saved->source->renderBlocks[previous],
                             now.structure->renderBlocks[current],
                             now.cachedLayerAxis==structure::LayerAxis::Material)){
                    same=false;break;
                }
            }
            if(!same)continue;
            if(oldSection>=saved->warningFill.size() ||
               oldSection>=saved->correctionOutline.size() ||
               oldSection>=saved->wrongFill.size() ||
               oldSection>=saved->wrongOutline.size() ||
               oldSection>=saved->nativeLiquid.size() ||
               oldSection>=saved->liquidProxy.size() ||
               oldSection>=saved->blockEntityPlaceholder.size() ||
               oldSection>=saved->praxisLiquid.size() ||
               oldSection>=saved->nativeLiquidCounts.size() ||
               oldSection>=saved->liquidProxyCounts.size() ||
               newSection>=now.warningFillSectionMeshes.size() ||
               newSection>=now.nativeLiquidSectionMeshes.size())continue;
            for(std::size_t i=0;i<beforeIndices.size();++i) {
                auto previous=beforeIndices[i],current=afterIndices[i];
                // CorrectionStateStore is paged and immutable to readers. Use its
                // copy-on-write setter rather than the removed vector assignment.
                auto const correction = saved->corrections.get(previous);
                now.correctionStates.set(current, correction);
                // These HUD counters must agree with the restored state before
                // the initial recheck runs, or a subsequent Correct->Missing
                // transition would underflow progressCorrectCount.
                auto const& cell = now.structure->renderBlocks[current];
                auto const axis = now.cachedLayerAxis.value_or(structure::LayerAxis::Y);
                auto const mode = now.cachedLayerDisplayMode.value_or(structure::LayerDisplayMode::All);
                bool const visible = isLayerVisible(
                    projectionLayer(*now.structure, cell, axis, now.cachedMirror, now.cachedRotation),
                    mode, now.cachedDisplayLayer, cell.materialIndex,
                    cell.liquidMaterialIndex, axis);
                if (correction == CorrectionState::Correct) {
                    now.progressCorrect[current] = 1;
                    ++now.progressCorrectCount;
                    if (visible) ++now.progressVisibleCorrectCount;
                }
                auto const errorKind = correctionProgressErrorKind(correction, visible);
                now.progressErrorKind[current] = errorKind;
                if (errorKind == 1) ++now.progressWrongTypeCount;
                else if (errorKind == 2) ++now.progressWrongStateCount;
                // Keep initial scan coverage pending: transferred results are
                // a provisional snapshot until the new world is rechecked.
                now.missingLayers[current]=saved->missingLayers[previous];
                now.blockActorRendererAvailable[current]=saved->actorRendererAvailable[previous];
            }
            after.meshes=std::move(before.meshes);
            after.blendSource=std::move(before.blendSource);
            after.blendSortKeyValid=false;
            after.blendSortUnsupported=false;
            now.warningFillSectionMeshes[newSection]=std::move(saved->warningFill[oldSection]);
            now.correctionOutlineSectionMeshes[newSection]=std::move(saved->correctionOutline[oldSection]);
            now.wrongFillSectionMeshes[newSection]=std::move(saved->wrongFill[oldSection]);
            now.wrongOutlineSectionMeshes[newSection]=std::move(saved->wrongOutline[oldSection]);
            now.nativeLiquidSectionMeshes[newSection]=std::move(saved->nativeLiquid[oldSection]);
            now.liquidProxySectionMeshes[newSection]=std::move(saved->liquidProxy[oldSection]);
            now.blockEntityPlaceholderSectionMeshes[newSection]=
                std::move(saved->blockEntityPlaceholder[oldSection]);
            now.praxisCompatLiquidSections[newSection]=std::move(saved->praxisLiquid[oldSection]);
            now.nativeLiquidSectionCellCounts[newSection]=saved->nativeLiquidCounts[oldSection];
            now.liquidProxySectionCellCounts[newSection]=saved->liquidProxyCounts[oldSection];
            after.dirty=false;
            after.incrementalDirty=false;
            after.buildInFlight=false;
            after.uploadedRevision=after.requestedRevision;
            ++now.meshUploadRevision;
            now.drawCache.noteMeshUpload(newSection);
            ++reused;
        }
        if(reused){
            ++now.progressRevision;
            now.praxisCompatLiquidAggregateDirty=true;
            now.praxisCompatLiquidAggregate.reset();
            now.praxisCompatLiquidAggregateOrder.clear();
        }
        return reused;
    } catch(...) {
        // Do not keep the pending transfer alive across a frame; untouched
        // new sections remain dirty and will take the normal build path.
        return 0;
    }
}

} // namespace lholo::projection::detail
