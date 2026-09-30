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
        auto const removed =
            projection::detail::projectionController().uninstallHooks();
        if (removed) {
            hook_lifecycle::waitForQuiescence();
            hook_lifecycle::markDisabled();
        }
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

    if (hook_lifecycle::insideDetour()) {
        logger.error("LHolo disable requested from inside a typed detour; refusing unsafe unload");
        return false;
    }

    // First make every newly-entering typed detour origin-only. Keep all world,
    // projection, ImGui, and worker state intact until the physical hooks are
    // detached and every callback that could still return through LHolo drains.
    hook_lifecycle::beginQuiesce();

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

    // Physical unhook prevents fresh entries. The guard counts both admitted
    // Running callbacks and Quiescing origin-only callbacks, so state is not
    // released until every already-entered typed detour has returned.
    hook_lifecycle::waitForQuiescence();

    // No typed render callback can reinstall the overlay after this point.
    // The overlay has its own callback drain for Present/WndProc/D3D detours and
    // the Praxis companion provider readers.
    if (!overlay::shutdown()) {
        logger.error(
            "LHolo disable aborted because overlay teardown was incomplete; native module remains resident"
        );
        return false;
    }

    // Cancellation alone is not enough: std::async may still be executing code
    // in this DLL. Join it before releasing structure or mapper state.
    structure::shutdownPendingStructureLoad();

    structure::saveSettings();
    structure::detail::shutdownMaterialTracker();
    place::resetWorldSession();
    structure::resetWorldSession();
    structure::capture::clear();

    hook_lifecycle::markDisabled();
    logger.info("LHolo disabled");
    return true;
}

} // namespace lholo::app
