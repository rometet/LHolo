// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "projection/ProjectionController.h"

#include "projection/hooks/ProjectionGameHooks.h"
#include "projection/hooks/ProjectionRenderHooks.h"
#include "projection/runtime/ProjectionSession.h"
#include "projection/runtime/ProjectionLifecycle.h"

#include "overlay/BoundsWireframe.h"

namespace lholo::projection::detail {

ProjectionController& projectionController() {
    static ProjectionController instance;
    return instance;
}

bool ProjectionController::installHooks() {
    if (!installProjectionGameHooks()) return false;
    // AppKernel owns rollback so it can publish Quiescing before any already
    // installed hook is physically removed.
    if (!installProjectionRenderHooks()) return false;
    return true;
}

bool ProjectionController::uninstallHooks() {
    bool ok = true;
    ok = uninstallProjectionRenderHooks() && ok;
    ok = uninstallProjectionGameHooks() && ok;
    return ok;
}

void ProjectionController::disableProjection() {
    auto& session = ProjectionSession::getInstance();
    session.withLockedState(
        [](ProjectionState& state, overlay::BoundsWireframe& captureBounds) {
            resetProjectionState(state);
            captureBounds.clear();
        }
    );
    // A requested restore anchor belongs only to the projection being
    // activated. Explicit disable/close must not leak it into a later load.
    session.cancelAnchorRequest();
    session.cancelDimensionSuspension();
}

} // namespace lholo::projection::detail
