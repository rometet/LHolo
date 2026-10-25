// Adapted from MarmieQi LHolo 26.51.4: projection frustum visibility.
// Preserve Praxis section owners and render/bucket masks.
#include "projection/runtime/ProjectionVisibility.h"
#include "projection/core/ProjectionState.h"
#include "projection/core/ProjectionVisibilityRules.h"
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
    bool changed=resetBounds;
    for(auto& sec:state.sections) {
        auto const visible=projectionBoundsVisible(frustum,sec.buildBounds,origin,2.0f);
        auto const prefetch=projectionBoundsVisible(frustum,sec.buildBounds,origin,16.0f);
        if(sec.renderVisible!=visible || sec.buildVisible!=visible ||
           sec.prefetchVisible!=prefetch)changed=true;
        sec.renderVisible=visible;
        sec.buildVisible=visible;
        sec.prefetchVisible=prefetch;
    }
    if(changed)++state.visibilityRevision;
}
}
