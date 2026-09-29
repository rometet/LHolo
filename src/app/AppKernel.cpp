// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "app/AppKernel.h"

#include "app/HookLifecycle.h"
#include "i18n/LanguageStore.h"
#include "input/MenuInputGuard.h"
#include "overlay/ImGuiOverlay.h"
#include "place/PlaceHelper.h"
#include "plugin/LHolo.h"
#include "projection/ProjectionController.h"
#include "projection/mesh/ProjectionMeshWorker.h"
#include "structure/capture/StructureCapture.h"
#include "structure/MaterialTracker.h"
#include "structure/StructureLoader.h"

#include "ll/api/mod/NativeMod.h"

namespace lholo::app {

AppKernel& AppKernel::getInstance() {
    static AppKernel instance;
    return instance;
}

bool AppKernel::load() {
    i18n::initLanguageStore();
    structure::loadSettings();
    return true;
}

bool AppKernel::enable() {
    auto& logger = LHolo::getInstance().getSelf().getLogger();

    if (!hook_lifecycle::beginEnable()) {
        logger.error("LHolo hook lifecycle was not ready for enable");
        return false;
    }

    projection::detail::resetMeshWorkerForSession();

    if (!projection::detail::projectionController().installHooks()) {
        hook_lifecycle::beginQuiesce();
        hook_lifecycle::waitForQuiescence();
        auto const removed =
            projection::detail::projectionController().uninstallHooks();
        if (removed) hook_lifecycle::markDisabled();
        logger.error(
            "Failed to install projection hooks; rollbackRemoved={}",
            removed ? 1 : 0
        );
        return false;
    }

    if (!place::installHook()) {
        logger.warn("Failed to install easy-place hooks");
    }

    auto const menuInputGuardStatus = input::installMenuInputGuard();
    if (!menuInputGuardStatus.mouseInputHookInstalled) {
        logger.warn("Failed to install menu mouse-input guard");
    }
    if (!menuInputGuardStatus.keyDownInputHookInstalled) {
        logger.warn("Failed to install menu key-down guard");
    }
    if (!menuInputGuardStatus.keyUpInputHookInstalled) {
        logger.warn("Failed to install menu key-up guard");
    }

    if (!overlay::ensureInstalled()) {
        logger.warn("GUI overlay hotkey hooks are not ready; lholo will retry initialization");
    }

    logger.info("LHolo enabled. Type lholo to open the projection menu.");
    logger.info("PHASE2_NATIVE_LIQUID_BUILD enabled=1 mesh=LHoloNativeLiquid");
    return true;
}

bool AppKernel::disable() {
    auto& logger = LHolo::getInstance().getSelf().getLogger();

    // Waiting for global detour quiescence from inside a typed detour would
    // wait for the current thread's own lease forever. Fail closed so the
    // loader keeps this native image resident and the user can restart cleanly.
    if (hook_lifecycle::insideDetour()) {
        logger.error("LHolo disable requested from inside a typed detour; refusing unsafe unload");
        return false;
    }

    // Close typed-detour admission first. Existing callbacks finish against
    // intact state; new callbacks become origin-only while teardown proceeds.
    hook_lifecycle::beginQuiesce();
    hook_lifecycle::waitForQuiescence();

    // Typed render hooks are now origin-only, so they can no longer retry
    // overlay installation. Drain Present/WndProc/Praxis callbacks before
    // mutating StructureSession or world-owned projection state.
    if (!overlay::shutdown()) {
        logger.error(
            "LHolo disable aborted because overlay teardown was incomplete; native module remains resident"
        );
        return false;
    }

    // Cancellation alone is not enough: std::async may still be executing code
    // in this DLL. Join it before any native image or loader state can vanish.
    structure::shutdownPendingStructureLoad();

    structure::saveSettings();
    structure::detail::shutdownMaterialTracker();
    // Drop all world-owned state only after every overlay callback has drained.
    // This resets held placement/input state and joins the projection mesh
    // worker while its Level/Dimension pointers are still valid.
    place::resetWorldSession();
    structure::resetWorldSession();
    structure::capture::clear();

    // Attempt every typed-hook teardown. If one hook refuses to detach,
    // returning false keeps the native module resident rather than leaving an
    // engine target pointing into an unloaded DLL.
    bool hooksRemoved = true;
    hooksRemoved = input::uninstallMenuInputGuard() && hooksRemoved;
    hooksRemoved = place::uninstallHook() && hooksRemoved;
    hooksRemoved = projection::detail::projectionController().uninstallHooks()
        && hooksRemoved;

    if (!hooksRemoved) {
        logger.error(
            "LHolo disable incomplete; one or more typed hooks remain installed and the native module must stay resident"
        );
        return false;
    }

    hook_lifecycle::markDisabled();
    logger.info("LHolo disabled");
    return true;
}

} // namespace lholo::app
