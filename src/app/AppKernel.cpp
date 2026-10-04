// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "app/AppKernel.h"

#include "app/HookLifecycle.h"
#include "app/InitializationRetention.h"
#include "app/MaterialExportDisable.h"
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
namespace {
// NativeModManager may destroy its temporary NativeMod after dynamic enable
// failure. Preserve the module/library/logger if rollback cannot detach every
// callback. Never release this last owner from inside the DLL being retained.
std::shared_ptr<ll::mod::NativeMod> gFailedInitializationOwner;
std::shared_ptr<ll::mod::NativeMod> gExportShutdownOwner;
}

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
    auto const owner = ll::mod::NativeMod::current();
    if (!owner) {
        logger.error("LHolo module ownership was unavailable for safe enable rollback");
        return false;
    }

    if (!hook_lifecycle::beginEnable()) {
        logger.error("LHolo hook lifecycle was not ready for enable");
        return false;
    }

    return initializeWithRetainedRollback(owner, gFailedInitializationOwner, [&] {
        if (!io::materialExportJob().openSession()) {
            logger.error("Material export from a previous session is still owned; refusing enable");
            return false;
        }
        projection::detail::resetMeshWorkerForSession();
        if (!projection::detail::projectionController().installHooks()) {
            logger.error("Failed to install required projection hooks");
            return false;
        }
        if (!place::installHook()) {
            logger.error("Failed to install required placement hooks");
            return false;
        }

        auto const menuInputGuardStatus = input::installMenuInputGuard();
        if (!menuInputGuardStatus.mouseInputHookInstalled
            || !menuInputGuardStatus.keyDownInputHookInstalled
            || !menuInputGuardStatus.keyUpInputHookInstalled) {
            logger.error("Failed to install all required menu input guards");
            return false;
        }
        if (!overlay::ensureInstalled()) {
            logger.warn("GUI overlay hotkey hooks are not ready; lholo will retry initialization");
        }
        logger.info("LHolo enabled. Type lholo to open the projection menu.");
        if(!menuInputGuardStatus.textInputHooksInstalled)
            logger.warn("Direct page hotkeys disabled: native text input tracking hooks are not ready");
        logger.info("PHASE2_NATIVE_LIQUID_BUILD enabled=1 mesh=LHoloNativeLiquid");
        return true;
    }, [this]() noexcept {
        // An install or even a diagnostic allocation can throw after earlier
        // hooks admitted callbacks. Drain the complete normal teardown once.
        bool cleaned{};
        auto const completed = invokeNativeCallback([&] {
            cleaned = disable();
            if (!cleaned) reportNativeCallbackFailure(
                "enable rollback", "teardown incomplete; native module must remain resident"
            );
        }, [](char const* reason) noexcept { reportNativeCallbackFailure("enable rollback", reason); });
        return completed && cleaned;
    }, [](char const* reason) noexcept { reportNativeCallbackFailure("enable", reason); });
}

bool AppKernel::disable() {
    auto& logger = LHolo::getInstance().getSelf().getLogger();

    if (hook_lifecycle::insideDetour()) {
        logger.error("LHolo disable requested from inside a typed detour; refusing unsafe unload");
        return false;
    }

    // A common save dialog (or filesystem operation) has no guaranteed OS
    // completion deadline. Cancel cooperatively and wait at most 250 ms. A
    // timeout returns BEFORE quiescing/removing hooks or releasing UI state.
    // The owned future and NativeMod stay resident; never detach that worker.
    auto const owner = ll::mod::NativeMod::current();
    if (!prepareMaterialExportDisable(io::materialExportJob(), owner, gExportShutdownOwner,
                                      std::chrono::milliseconds{250})) {
        logger.error("LHolo disable refused: material export is cancelling/finishing or module ownership is unavailable. Hooks remain intact; close any save prompt and retry disable/unload.");
        return false;
    }

    // First make every newly-entering typed detour origin-only. Keep world and
    // projection state intact through both admitted-body and origin-only drains.
    hook_lifecycle::beginQuiesce();
    // Admitted bodies still need nested virtual-world query/setBlock hooks.
    // Drain them while those dependencies remain installed; then join pending
    // workers and release their captured native references before unhooking.
    hook_lifecycle::waitForRunningCallbacks();
    projection::detail::stopMeshWorker();

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
    structure::capture::shutdown();

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

bool AppKernel::unload() {
    auto& logger = LHolo::getInstance().getSelf().getLogger();
    // RegisterHelper binds this boolean onUnload callback. Reject attempts
    // that bypass disable or follow any incomplete teardown.
    // Even an unload that bypasses disable cancels/closes export admission and
    // retains the module if that worker has not returned. This gate never waits.
    bool const exportDrained=prepareMaterialExportDisable(io::materialExportJob(), ll::mod::NativeMod::current(),
                                                          gExportShutdownOwner, std::chrono::milliseconds{0});
    if (!exportDrained || hook_lifecycle::state() != hook_lifecycle::State::Disabled) {
        logger.error("LHolo unload refused: disable/owned material export drain must complete first");
        return false;
    }
    return true;
}

} // namespace lholo::app
