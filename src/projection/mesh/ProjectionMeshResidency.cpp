#include "projection/mesh/ProjectionMeshResidency.h"
#include "projection/core/ProjectionState.h"

#include <algorithm>

namespace lholo::projection::detail {
namespace {
bool hasResidentGeometry(ProjectionState const& state, std::size_t index) {
    auto const& section = state.sections[index];
    if (std::any_of(section.meshes.begin(), section.meshes.end(),
                    [](auto const& mesh) { return bool(mesh); }))
        return true;
    if (section.mixedGlassLiquid && section.mixedGlassLiquid->mesh) return true;
    if (section.blendSource) return true;
    auto const owned = [](auto const& mesh) { return bool(mesh); };
    if (index < state.warningFillSectionMeshes.size()
        && owned(state.warningFillSectionMeshes[index])) return true;
    if (index < state.correctionOutlineSectionMeshes.size()
        && owned(state.correctionOutlineSectionMeshes[index])) return true;
    if (index < state.wrongFillSectionMeshes.size()
        && owned(state.wrongFillSectionMeshes[index])) return true;
    if (index < state.wrongOutlineSectionMeshes.size()
        && owned(state.wrongOutlineSectionMeshes[index])) return true;
    if (index < state.blockEntityPlaceholderSectionMeshes.size()
        && owned(state.blockEntityPlaceholderSectionMeshes[index])) return true;
    if (index < state.nativeLiquidSectionMeshes.size()
        && owned(state.nativeLiquidSectionMeshes[index])) return true;
    if (index < state.praxisCompatLiquidSections.size()
        && owned(state.praxisCompatLiquidSections[index])) return true;
    if (index < state.liquidProxySectionMeshes.size()
        && owned(state.liquidProxySectionMeshes[index])) return true;
    return false;
}
}

// Bounded per-frame release of GPU geometry outside the retention ring. The
// distance authority is the same fail-open classification that visibility
// uses (native far clip in this fork). Renewed requests keep the cursor, so
// continuous camera movement cannot starve the end of a large blueprint.
void trimProjectionMeshResidency(ProjectionState& state) {
    std::size_t probes{}, released{};
    while (state.residencyScan.pending() && probes < ProjectionResidencyProbesPerFrame
           && released < ProjectionResidencyReleasesPerFrame) {
        auto const index = state.residencyScan.next();
        ++probes;
        if (index >= state.sections.size()) continue;
        auto& section = state.sections[index];
        if (section.withinRetentionDistance || !hasResidentGeometry(state, index)) continue;
        for (auto& mesh : section.meshes) mesh.reset();
        section.blendSource.reset();
        section.blendSortKeyValid = false;
        section.mixedGlassLiquid.reset();
        if (index < state.warningFillSectionMeshes.size()) state.warningFillSectionMeshes[index].reset();
        if (index < state.correctionOutlineSectionMeshes.size()) state.correctionOutlineSectionMeshes[index].reset();
        if (index < state.wrongFillSectionMeshes.size()) state.wrongFillSectionMeshes[index].reset();
        if (index < state.wrongOutlineSectionMeshes.size()) state.wrongOutlineSectionMeshes[index].reset();
        if (index < state.blockEntityPlaceholderSectionMeshes.size()) state.blockEntityPlaceholderSectionMeshes[index].reset();
        // Evicting typed liquid streams must also retire their derived, cross-section
        // aggregate and boundary mask; otherwise the aggregate retains evicted
        // section data and can be reused after the section goes out of range.
        bool const hadTypedLiquid =
            (index < state.nativeLiquidSectionMeshes.size() && state.nativeLiquidSectionMeshes[index]) ||
            (index < state.praxisCompatLiquidSections.size() && state.praxisCompatLiquidSections[index]);
        if (index < state.nativeLiquidSectionMeshes.size()) state.nativeLiquidSectionMeshes[index].reset();
        if (index < state.praxisCompatLiquidSections.size()) state.praxisCompatLiquidSections[index].reset();
        if (index < state.liquidProxySectionMeshes.size()) state.liquidProxySectionMeshes[index].reset();
        if (index < state.nativeLiquidSectionCellCounts.size()) state.nativeLiquidSectionCellCounts[index] = 0;
        if (index < state.liquidProxySectionCellCounts.size()) state.liquidProxySectionCellCounts[index] = 0;
        if (hadTypedLiquid) {
            state.praxisCompatLiquidAggregate.reset();
            state.praxisCompatLiquidAggregateOrder.clear();
            state.praxisCompatLiquidBoundaryMaskCache.clear();
            state.praxisCompatLiquidAggregateDirty = true;
        }
        // Batched CPU sources referenced the evicted meshes; the caches
        // rebuild lazily on the next draw, exactly like invalidation does.
        state.correctionDrawBatches.clear();
        state.transparentDrawBatches.clear();
        // A released section rebuilds with fresh bounds when it returns.
        section.meshBounds = {};
        section.dirty = section.modelDirty = true;
        section.uploadedRevision = 0;
        ++section.requestedRevision;
        ++state.meshUploadRevision;
        // Revision bumps must name the evicted section even when other sections
        // uploaded this frame: otherwise incremental draw lists keep stale slots.
        state.drawCache.noteMeshUpload(index);
        ++released;
    }
}
} // namespace lholo::projection::detail
