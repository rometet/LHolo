[Reading 54 lines from start (total: 54 lines, 0 remaining)]

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
    if (!installProjectionRenderHooks()) {
        uninstallProjectionGameHooks();
        return false;
    }
    return true;
}

bool ProjectionController::uninstallHooks() {
    bool ok = true;
    ok = uninstallProjectionRenderHooks() && ok;
    ok = uninstallProjectionGameHooks() && ok;
    ProjectionSession::getInstance().withLockedState(
        [](ProjectionState&, overlay::BoundsWireframe& captureBounds) {
            captureBounds.clear();
        }
    );
    return ok;
}

void ProjectionController::disableProjection() {
    auto& session = ProjectionSession::getInstance();
    session.withLockedState(
        [](ProjectionState& state, overlay::BoundsWireframe&) {
            resetProjectionState(state);
        }
    );
    // A requested restore anchor belongs only to the projection being
    // activated. Explicit disable/close must not leak it into a later load.
    session.cancelAnchorRequest();
    session.cancelDimensionSuspension();
}

} // namespace lholo::projection::detail

[executed on device: ちひろのPC (a22d5426-96cc-488b-9398-cec6fdb0f382)]