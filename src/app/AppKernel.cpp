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
        logger.error("LHolo hook lifecycle is not ready for enable");
        return false;
    }

    projection::detail::resetMeshWorkerForSession();

    if (!projection::detail::projectionController().installHooks()) {
        // A failed install may leave a subset of hooks registered. Quiesce
        // first, then remove every tracked hook before allowing the DLL to
        // become disabled.
        hook_lifecycle::beginQuiesce();
        auto const projectionRemoved =
            projection::detail::projectionController().uninstallHooks();
        auto const placeRemoved = place::uninstallHook();
        auto const inputRemoved = input::uninstallMenuInputGuard();
        if (projectionRemoved && placeRemoved && inputRemoved) {
            hook_lifecycle::waitForQuiescence();
            if (overlay::shutdown()) {
                hook_lifecycle::markDisabled();
            }
        }
        logger.error("Failed to install projection hooks");
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

    // Publish quiescing before touching hook registrations. Every LHolo detour
    // becomes an origin-only pass-through immediately, including calls that
    // enter through another mod's hook chain.
    hook_lifecycle::beginQuiesce();
    structure::saveSettings();

    bool hooksRemoved = true;
    hooksRemoved = input::uninstallMenuInputGuard() && hooksRemoved;
    hooksRemoved = place::uninstallHook() && hooksRemoved;
    hooksRemoved = projection::detail::projectionController().uninstallHooks()
        && hooksRemoved;
    if (!hooksRemoved) {
        // Fail closed: state, workers and overlay resources stay alive while a
        // callback may still target LHolo.dll. A later disable attempt can
        // retry only the hooks whose tracked state is still installed.
        logger.error(
            "LHolo disable aborted because one or more native hooks could not be removed"
        );
        return false;
    }

    // No new tracked detours can enter after successful physical unhook. Wait
    // for callbacks that were already inside LHolo to return before releasing
    // any projection-owned state.
    hook_lifecycle::waitForQuiescence();

    // The overlay has its own MinHook/WndProc callbacks. Its shutdown is
    // checked and fail-closed as well; never destroy state under a stale
    // executable callback.
    if (!overlay::shutdown()) {
        logger.error("LHolo disable aborted because overlay teardown was incomplete");
        return false;
    }

    structure::detail::shutdownMaterialTracker();
    place::resetWorldSession();
    structure::resetWorldSession();
    structure::capture::clear();

    hook_lifecycle::markDisabled();
    logger.info("LHolo disabled");
    return true;
}

} // namespace lholo::app
