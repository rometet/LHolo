// Adapted from MarmieQi LHolo 26.51.4: projection frustum visibility.
// Preserve Praxis section owners and render/bucket masks.
#include "projection/runtime/ProjectionVisibility.h"
#include "projection/core/ProjectionState.h"
#include "projection/core/ProjectionVisibilityRules.h"
#include "resourcepacks/WorldPackReloadState.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/BaseActorRenderContext.h"
#include "mc/deps/renderer/Camera.h"

namespace lholo::projection::detail {
void updateProjectionVisibility(ProjectionState& state, BaseActorRenderContext& context, Vec3 const& camera) {
    // Recompute transformed section bounds only after layout or structure
    // changes. A missing source bound must stay visible (conservative).
    auto const resetLayout=
        state.visibilityBoundsGeneration != state.structureGeneration ||
        state.visibilityBoundsRotation != state.cachedRotation ||
        state.visibilityBoundsMirror != state.cachedMirror ||
        state.visibilityBoundsSections > state.sections.size();
    auto const from=resetLayout?std::size_t{0}:state.visibilityBoundsSections;
    bool const resetBounds=resetLayout || from<state.sections.size();
    if (resetBounds) {
        // Incremental section creation must not recompute all previous AABBs,
        // otherwise N arriving chunks would trigger O(N^2) transforms.
        for(std::size_t i=from;i<state.sections.size();++i) {
            auto& sec=state.sections[i];
            sec.buildBounds={};
            if(state.structure && i<state.localSectionKeys.size()
                && state.cachedRotation>=0 && state.cachedMirror>=0) {
                sec.buildBounds=projectionSectionBounds(
                    state.localSectionKeys[i],*state.structure,
                    state.cachedMirror,state.cachedRotation);
            }
        }
        state.visibilityBoundsGeneration=state.structureGeneration;
        state.visibilityBoundsSections=state.sections.size();
        state.visibilityBoundsRotation=state.cachedRotation;
        state.visibilityBoundsMirror=state.cachedMirror;
    }
    ProjectionFrustum frustum;
    auto const& cam=context.mScreenContext.camera;
    if(!cam.viewMatrixStack->stack->empty() &&
       !cam.projectionMatrixStack->stack->empty()) {
        frustum=ProjectionFrustum::fromViewProjection(
            cam.getProjectionMatrix()*(*cam.viewMatrixStack->top()._m));
    }
    // Camera-relative view-projection: source bounds are local to projection.
    Vec3 const origin{
        static_cast<float>(state.anchor.x+state.cachedOffsetX)-camera.x,
        static_cast<float>(state.anchor.y+state.cachedOffsetY)-camera.y,
        static_cast<float>(state.anchor.z+state.cachedOffsetZ)-camera.z};
    float const farClip=cam.mZFar;
    bool changed=resetBounds;
    bool retentionFlipped=false;
    for(auto& sec:state.sections) {
        auto const view=classifyProjectionSection(frustum,sec.buildBounds,origin,farClip);
        if(sec.withinRetentionDistance!=view.withinRetentionDistance)retentionFlipped=true;
        bool const renderVisible = view.renderVisible && !sec.layerStale;
        if(sec.renderVisible!=renderVisible
            || sec.buildVisible!=view.buildVisible
            || sec.prefetchVisible!=view.prefetchVisible
            || sec.withinBuildDistance!=view.withinBuildDistance
            || sec.withinRetentionDistance!=view.withinRetentionDistance)changed=true;
        sec.renderVisible=renderVisible;
        sec.buildVisible=view.buildVisible;
        sec.prefetchVisible=view.prefetchVisible;
        sec.withinBuildDistance=view.withinBuildDistance;
        sec.withinRetentionDistance=view.withinRetentionDistance;
    }
    if(changed)++state.visibilityRevision;
    // A section that left the retention ring must be released by the bounded
    // scan; sections that came back simply re-enter the build admission.
    if(retentionFlipped)state.residencyScan.request(state.sections.size());
    // Retire at most two out-of-range Blend GPU streams per frame, without
    // touching the original opaque/correction/liquid owners. Rebuild the
    // section when it returns to range. Never retire during pack reload.
    auto const reload=resourcepacks::state().snapshot();
    bool const mutationActive=state.placementBuildActive
        || bool(state.pendingRegionReuse) || (reload && reload->closed);
    std::size_t retired{};
    auto const blendBucket=static_cast<std::size_t>(RenderBucket::Blend);
    for(std::size_t i=0;i<state.sections.size() && retired<2;++i) {
        auto& section=state.sections[i];
        auto& blend=section.meshes[blendBucket];
        if(!shouldRetireDistantTransparentMesh(
            section.withinRetentionDistance,section.dirty,
            section.buildInFlight,bool(blend || section.blendSource),mutationActive))
            continue;
        blend.reset();
        section.blendSource.reset();
        section.blendSortOrder.clear();
        section.blendSortKeyValid=false;
        section.blendSortUnsupported=false;
        ++section.blendSortUploadGeneration;
        // A new full native build restores every layer while the old opaque
        // meshes remain drawable. Worker revisions reject older results.
        section.dirty=true;
        section.incrementalDirty=false;
        ++section.requestedRevision;
        ++state.meshUploadRevision;
        state.blendSourceScanValid=false;
        state.drawCache.noteMeshUpload(i);
        state.transparentDrawBatches.clear();
        state.meshPreflightDone=false;
        ++retired;
    }
}
}
