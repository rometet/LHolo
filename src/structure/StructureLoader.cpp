// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "structure/StructureLoader.h"
#include "app/ScopeExit.h"
#include "app/FutureResult.h"
#include "app/NativeCallbackBoundary.h"

#include "i18n/Message.h"
#include "input/ViewMoveBasis.h"
#include "settings/SettingsStore.h"
#include "structure/MaterialTracker.h"
#include "structure/formats/StructureFormatLoaders.h"
#include "structure/StructureSession.h"
#include "structure/SchematicRuntime.h"
#include "structure/LoadIntent.h"
#include "structure/StructurePaths.h"
#include "structure/StructureUiState.h"
#include "overlay/CompanionBridge.h"
#include "ui/HudSummaryPolicy.h"
#include "ui/HotkeyFormat.h"
#include "ui/MenuController.h"
#include "structure/capture/StructureCapture.h"
#include "structure/java_to_bedrock/JavaToBedrock.h"
#include "ui/FileDialog.h"
#include "ui/FluentTheme.h"
#include "ui/LHoloMenu.h"
#include "ui/MenuWidgets.h"
#include "place/PlaceHelper.h"
#include "plugin/LHolo.h"
#include "projection/Projection.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <future>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

#include <Windows.h>

#include "ll/api/mod/NativeMod.h"
#include "imgui.h"

namespace lholo::structure {
namespace {

constexpr std::size_t kGuiHotkeyIndex = input::hotkeyIndex(input::HotkeyId::Gui);
constexpr std::size_t kLayerIncreaseHotkeyIndex = input::hotkeyIndex(input::HotkeyId::LayerIncrease);
constexpr std::size_t kLayerDecreaseHotkeyIndex = input::hotkeyIndex(input::HotkeyId::LayerDecrease);
constexpr std::size_t kLoadProjectionHotkeyIndex = input::hotkeyIndex(input::HotkeyId::LoadProjection);
constexpr std::size_t kCloseProjectionHotkeyIndex = input::hotkeyIndex(input::HotkeyId::CloseProjection);
constexpr std::size_t kToggleManualPlacementHotkeyIndex
    = input::hotkeyIndex(input::HotkeyId::ToggleManualPlacement);
constexpr float kActionHintVerticalScreenRatio = 0.80f;
// Background alpha shared by the projection HUD, the material HUD and the
// transient action hint. The three on-screen bars deliberately share one
// value so they read as a single overlay instead of three separate widgets;
// the hint starts from this value and fades out from there.
constexpr float kOverlayWindowBgAlpha = 0.68f;
auto& logger() {
    return LHolo::getInstance().getSelf().getLogger();
}

bool hudContextAvailable() {
    return capture::getClientViewSnapshot().has_value() && !projection::isDimensionSuspended()
        && detail::StructureSession::getInstance().visible();
}

auto& uiState() {
    return detail::StructureUiState::getInstance();
}

std::filesystem::path settingsPath() {
    return LHolo::getInstance().getSelf().getConfigDir() / "config.json";
}

unsigned int currentHotkeyModifiers() {
    return uiState().currentHotkeyModifiers();
}

enum class PendingStructureLoadMode : std::uint8_t {
    Normal,
    Restore,
};

struct PreparedStructureLoadResult {
    std::shared_ptr<detail::PreparedStructureLoad> prepared;
    std::string                                    error;
};

struct PendingStructureLoad {
    PendingStructureLoadMode            mode{PendingStructureLoadMode::Normal};
    std::string                         path;
    detail::SavedProjectionSnapshot     saved;
    ProjectionRequestStamp             request;
    std::future<PreparedStructureLoadResult> future;
    std::chrono::steady_clock::time_point started;
    std::uint64_t                       sourceWorldEpoch{};
    bool                                hasSourceWorld{};
    std::uint64_t                       ticket{};
};

// Only the Present owner (or shutdown after callback drain) accesses futures.
// The latest queued async request replaces earlier queued requests without
// joining the running parse on the frame thread or creating unbounded jobs.
std::optional<PendingStructureLoad> gPendingStructureLoad;
std::optional<PendingStructureLoad> gQueuedStructureLoad;
detail::LoadIntent gLoadIntent;

void commitNormalStructureLoad(
    std::shared_ptr<LoadedStructure> loaded,
    std::string const&               pathText,
    ProjectionRequestStamp           request
) {
    auto& session = detail::StructureSession::getInstance();
    auto const renderBlocks = loaded->renderBlocks.size();
    auto const status = makeLoadedStatusMessage(*loaded);

    projection::cancelNextStructureAnchorRequest();
    session.resetTransform();
    session.replaceLoaded(std::move(loaded), pathText, status, request);
    detail::invalidateMaterialList();
    saveSettings();
    logger().info("Loaded structure {}: {} renderable blocks", pathText, renderBlocks);
}

void commitRestoredStructureLoad(
    std::shared_ptr<LoadedStructure> loaded,
    detail::SavedProjectionSnapshot const& saved,
    ProjectionRequestStamp                request
) {
    auto& session = detail::StructureSession::getInstance();
    session.setRotation(saved.transform.rotation);
    session.setMirror(std::clamp(saved.transform.mirror, 0, 2));
    session.setOffsetX(saved.transform.offsetX);
    session.setOffsetY(saved.transform.offsetY);
    session.setOffsetZ(saved.transform.offsetZ);
    session.setLayerDisplayMode(saved.transform.layerDisplayMode);
    session.setDisplayLayer(saved.transform.displayLayer);
    session.setLayerAxis(saved.transform.layerAxis);
    session.setVisible(saved.transform.visible); session.setCountExtras(saved.transform.countExtras);
    projection::requestNextStructureAnchor(
        saved.anchorX, saved.anchorY, saved.anchorZ
    );
    session.replaceLoaded(
        std::move(loaded),
        saved.structurePath,
        i18n::Message{i18n::TextKey::StatusRestoredPending},
        request
    );
    detail::invalidateMaterialList();
    logger().info(
        "Restoring projection {} at ({}, {}, {})",
        saved.structurePath,
        saved.anchorX,
        saved.anchorY,
        saved.anchorZ
    );
}

bool launchPreparedStructureLoad(PendingStructureLoad pending) {
    auto const request=pending.request;bool launched{};
    app::ScopeExit retireFailed([&]() noexcept {if(!launched)schematic::failProjectionRequest(request);});
    auto path = detail::pathFromUtf8(pending.path);
    try {
        pending.future = std::async(
        std::launch::async,
        [path = std::move(path)]() mutable {
            PreparedStructureLoadResult result;
            try {
                result.prepared = detail::prepareStructureFile(path, result.error);
            } catch (std::exception const& exception) {
                result.error = exception.what();
            } catch (...) {
                result.error = "结构后台预解析发生未知异常";
            }
            return result;
        }
        );
    } catch (std::exception const& exception) {
        auto const message = pending.mode == PendingStructureLoadMode::Restore
            ? i18n::Message{i18n::TextKey::StatusRestoreFailed, {exception.what()}}
            : i18n::Message{i18n::TextKey::StatusLoadFailed, {exception.what()}};
        gLoadIntent.applyIfCurrent(pending.ticket, [&] {
            detail::StructureSession::getInstance().setStatus(message);
        });
        logger().error("Could not start background structure load {}: {}", pending.path, exception.what());
        return false;
    }
    gPendingStructureLoad.emplace(std::move(pending));
    launched=true;
    logger().info("Preparing structure asynchronously: {}", gPendingStructureLoad->path);
    return true;
}

bool startPreparedStructureLoad(
    PendingStructureLoadMode mode,
    std::string pathText,
    std::uint64_t ticket,
    detail::SavedProjectionSnapshot const& saved = {},
    ProjectionRequestStamp request = {}
) {
    PendingStructureLoad pending;
    pending.mode = mode;
    pending.path = std::move(pathText);
    pending.ticket = ticket;
    pending.saved = saved;
    pending.request = request;
    pending.started = std::chrono::steady_clock::now();
    if (auto const view = capture::getClientViewSnapshot()) {
        pending.sourceWorldEpoch = view->worldEpoch;
        pending.hasSourceWorld = true;
    }
    if (gPendingStructureLoad) {
        logger().info("Queued latest structure load: {}", pending.path);
        gQueuedStructureLoad.emplace(std::move(pending));
    } else {
        return launchPreparedStructureLoad(std::move(pending));
    }
    return true;
}

void processCompletedStructureLoad() {
    if (!gPendingStructureLoad) return;
    if (gPendingStructureLoad->future.wait_for(std::chrono::milliseconds{0})
        != std::future_status::ready) {
        return;
    }

    // get() consumes the future even when it throws. Retire the slot before
    // fallible error/status formatting, preserving admission of the next job.
    auto pending = app::takePendingValue(gPendingStructureLoad);
    bool committed{};
    app::ScopeExit retireFailed([&]() noexcept {if(!committed)schematic::failProjectionRequest(pending.request);});
    auto const mode = pending.mode;
    auto const& path = pending.path;
    auto const& saved = pending.saved;
    auto const started = pending.started;
    auto const sourceWorldEpoch = pending.sourceWorldEpoch;
    auto const hasSourceWorld = pending.hasSourceWorld;
    auto const ticket = pending.ticket;

    PreparedStructureLoadResult preparedResult;
    try {
        preparedResult = pending.future.get();
    } catch (std::exception const& exception) {
        preparedResult.error = exception.what();
    } catch (...) {
        preparedResult.error = "结构后台预解析发生未知异常";
    }
    if (!gLoadIntent.current(ticket)) return;

    // A prepared NBT tree is world-independent, but palette finalization is
    // not. Never resolve an old-world request against a newly joined world or
    // a different dimension.
    if (hasSourceWorld) {
        auto const view = capture::getClientViewSnapshot();
        if (!view || view->worldEpoch != sourceWorldEpoch) {
            logger().info(
                "Discarded prepared structure {} because the world/dimension changed",
                path
            );
            return;
        }
    }

    auto& session = detail::StructureSession::getInstance();
    if (!preparedResult.prepared) {
        auto const message = mode == PendingStructureLoadMode::Restore
            ? i18n::Message{i18n::TextKey::StatusRestoreFailed, {preparedResult.error}}
            : i18n::Message{i18n::TextKey::StatusLoadFailed, {preparedResult.error}};
        gLoadIntent.applyIfCurrent(ticket, [&] { session.setStatus(std::move(message)); });
        logger().error("Could not prepare structure {}: {}", path, preparedResult.error);
        return;
    }

    auto const finalizeStarted = std::chrono::steady_clock::now();
    std::string error;
    auto loaded = detail::finalizePreparedStructureFile(
        std::move(preparedResult.prepared), error
    );
    auto const finalized = std::chrono::steady_clock::now();
    if (hasSourceWorld) {
        auto const view = capture::getClientViewSnapshot();
        if (!view || view->worldEpoch != sourceWorldEpoch) {
            logger().info("Discarded finalized structure {} because the world/dimension changed", path);
            return;
        }
    }
    if (!loaded) {
        auto const message = mode == PendingStructureLoadMode::Restore
            ? i18n::Message{i18n::TextKey::StatusRestoreFailed, {error}}
            : i18n::Message{i18n::TextKey::StatusLoadFailed, {error}};
        gLoadIntent.applyIfCurrent(ticket, [&] { session.setStatus(std::move(message)); });
        logger().error("Could not finalize structure {}: {}", path, error);
        return;
    }

    auto const prepareMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        finalizeStarted - started
    ).count();
    auto const finalizeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        finalized - finalizeStarted
    ).count();
    logger().info(
        "Structure staged load timings: background={}ms native-finalize={}ms",
        prepareMs,
        finalizeMs
    );

    gLoadIntent.applyIfCurrent(ticket, [&] {
        if (mode == PendingStructureLoadMode::Restore) {
            commitRestoredStructureLoad(std::move(loaded), saved, pending.request);
        } else {
            commitNormalStructureLoad(std::move(loaded), path, pending.request);
        }
        committed=true;
    });
}

void processPreparedStructureLoad() {
    processCompletedStructureLoad();
    if (!gPendingStructureLoad && gQueuedStructureLoad) {
        auto queued = std::move(*gQueuedStructureLoad);
        gQueuedStructureLoad.reset();
        if (gLoadIntent.current(queued.ticket)) {
            launchPreparedStructureLoad(std::move(queued));
        }
    }
}

// Resolve one move hotkey into a world-space step and queue it. The direction
// uses the last game tick's copied view. Native player access from WndProc or
// Present would outlive an unowned borrow during world/player teardown.
bool queueViewRelativeMove(input::HotkeyId move) {
    auto const view = capture::getClientViewSnapshot();
    if (!view) return false;
    auto const step = input::viewRelativeMoveStep(move, view->yaw);
    if (!step.valid) return false;
    uiState().queueOffsetDelta(step.dx, step.dy, step.dz);
    return true;
}

} // namespace

void requestMaterialList() {
    detail::requestMaterialListRefresh();
}

void requestOpenGui() {
    auto const opening = uiState().toggleGuiVisible();
    if (opening) {
        uiState().setOpeningInputBlockFrames(3);
    } else {
        // Consume the release half of the key/click that closed the menu.
        // Without this, Minecraft receives an unmatched Esc or mouse-up after
        // the ImGui window has already disappeared.
        uiState().setBlockGameInputUntil(GetTickCount64() + 180);
    }
}

bool isGuiVisible() { return uiState().guiVisible(); }

bool shouldShowProjectedBlockName() {
    auto const hud = uiState().hud();
    return hud.enabled && hud.showProjectedBlockName;
}

bool isInputTransitionBlocked() {
    return GetTickCount64() <= uiState().blockGameInputUntil();
}

bool isMenuInputCaptured() {
    return isGuiVisible() || isInputTransitionBlocked();
}

static bool requestMenuRoute(input::MenuRoute route,bool allowDirectRoutes,std::uintptr_t gameWindow) {
    if(route<input::MenuRoute::Placed || route>input::MenuRoute::Materials)return false;
    auto const generation=uiState().menuRouteGeneration();
    auto const view=capture::getClientViewSnapshot();
    return capture::publishMenuRouteIfCurrent(view,[&](auto const& current){
        auto const now=GetTickCount64();
        bool const gameInput=current && now>=current->sampledAtMillis && now-current->sampledAtMillis<=250 && current->gameplayInputEnabled;
        input::DirectMenuInputContext const context{allowDirectRoutes && input::directMenuForegroundMatches(gameWindow,reinterpret_cast<std::uintptr_t>(GetForegroundWindow())),overlay::companion::isVisible(),isGuiVisible(),gameInput,
            uiState().uiInteractionBlocked(),uiState().nativeTextInputBlocked()};
        if(!input::directMenuInputAllowed(context))return false;
        return uiState().queueMenuRoute({route,generation,current?current->worldEpoch:0,gameWindow});
    });
}

bool handleGuiHotkeyKeyDown(unsigned int virtualKey, bool allowDirectRoutes,std::uintptr_t gameWindow) {
    auto const modifierKey = ui::isModifierKey(virtualKey);
    if (virtualKey == VK_CONTROL || virtualKey == VK_LCONTROL || virtualKey == VK_RCONTROL) {
        uiState().setControlHeld(true);
    } else if (virtualKey == VK_MENU || virtualKey == VK_LMENU || virtualKey == VK_RMENU) {
        uiState().setAltHeld(true);
    } else if (virtualKey == VK_SHIFT || virtualKey == VK_LSHIFT || virtualKey == VK_RSHIFT) {
        uiState().setShiftHeld(true);
    }

    // Praxis owns input while its menu is visible, including its shared HUD
    // binding editor. Keep modifier tracking above and key-up release active.
    if (overlay::companion::isVisible()) {
        uiState().stopHotkeyCapture();
        return false;
    }

    auto const captureIndex = uiState().capturingHotkey();
    if (captureIndex) {
        // F11 belongs to Minecraft's fullscreen toggle. Never capture or
        // consume it as a mod shortcut, including while rebinding controls.
        if (virtualKey == VK_F11) return false;
        if (virtualKey == VK_ESCAPE) {
            uiState().stopHotkeyCapture();
        } else if (virtualKey == VK_DELETE || virtualKey == VK_BACK) {
            uiState().clearHotkey(*captureIndex);
            uiState().stopHotkeyCapture();
            uiState().requestSettingsSave();
        } else if (!modifierKey) {
            auto const modifiers = currentHotkeyModifiers();
            uiState().bindCapturedHotkey(*captureIndex, virtualKey, modifiers);
            uiState().setIgnoreHotkeyUntil(GetTickCount64() + 250);
            uiState().requestSettingsSave();
        }
        return true;
    }

    // The projection-offset trigger is the fixed Alt key. Only claim the bare
    // key while the gesture is enabled and an active projection can actually
    // consume its wheel; otherwise preserve Minecraft and system Alt handling
    // unchanged.
    if ((virtualKey == VK_MENU || virtualKey == VK_LMENU || virtualKey == VK_RMENU)
        && uiState().altWheelOffsetEnabled()
        && detail::StructureSession::getInstance().hasLoaded()) {
        return true;
    }

    if (modifierKey) return false;

    auto const modifiers = currentHotkeyModifiers();
    auto const guiHotkey = uiState().inputHotkey(kGuiHotkeyIndex);
    if (guiHotkey.key != 0 && virtualKey == guiHotkey.key
        && modifiers == guiHotkey.modifiers) {
        if (GetTickCount64() >= uiState().ignoreHotkeyUntil()
            && uiState().tryPressHotkey(kGuiHotkeyIndex)) {
            requestOpenGui();
        }
        return true;
    }
    auto const now=GetTickCount64();
    auto const view=capture::getClientViewSnapshot();
    bool const gameInput=view && now>=view->sampledAtMillis
        && now-view->sampledAtMillis<=250 && view->gameplayInputEnabled;
    input::DirectMenuInputContext const directContext{
        allowDirectRoutes,false,isGuiVisible(),gameInput,
        uiState().uiInteractionBlocked(),uiState().nativeTextInputBlocked()};
    if(input::directMenuInputAllowed(directContext) && virtualKey!=VK_F11) {
        for(std::size_t index=input::kDirectMenuHotkeyFirst;index<input::kHotkeyCount;++index) {
            auto const hotkey=uiState().inputHotkey(index);
            if(!hotkey.key || hotkey.key!=virtualKey || hotkey.modifiers!=modifiers)continue;
            if(uiState().firstHotkeyConflict(index))continue;
            if(now>=uiState().ignoreHotkeyUntil() && uiState().tryPressHotkey(index)) {
                if(!requestMenuRoute(input::menuRouteForHotkey(index),allowDirectRoutes,gameWindow)) {
                    uiState().releaseHotkey(index);
                    return false;
                }
            }
            return true;
        }
    }
    if (isGuiVisible()) return false;

    for (std::size_t index = 0; index < input::kMoveHotkeyCount; ++index) {
        auto const hotkey = uiState().inputHotkey(index + input::kMoveHotkeyFirst);
        if (hotkey.key == virtualKey && hotkey.modifiers == modifiers) {
            if (GetTickCount64() >= uiState().ignoreHotkeyUntil()
                && uiState().tryPressHotkey(index + input::kMoveHotkeyFirst)) {
                queueViewRelativeMove(
                    static_cast<input::HotkeyId>(index + input::kMoveHotkeyFirst)
                );
            }
            return true;
        }
    }

    auto const layerIncreaseHotkey = uiState().inputHotkey(kLayerIncreaseHotkeyIndex);
    if (layerIncreaseHotkey.key != 0 && virtualKey == layerIncreaseHotkey.key
        && modifiers == layerIncreaseHotkey.modifiers) {
        if (!detail::StructureSession::getInstance().layerDisplayEnabled()) return false;
        if (GetTickCount64() >= uiState().ignoreHotkeyUntil()
            && uiState().tryPressHotkey(kLayerIncreaseHotkeyIndex)) {
            uiState().queueLayerDelta(1);
        }
        return true;
    }
    auto const layerDecreaseHotkey = uiState().inputHotkey(kLayerDecreaseHotkeyIndex);
    if (layerDecreaseHotkey.key != 0 && virtualKey == layerDecreaseHotkey.key
        && modifiers == layerDecreaseHotkey.modifiers) {
        if (!detail::StructureSession::getInstance().layerDisplayEnabled()) return false;
        if (GetTickCount64() >= uiState().ignoreHotkeyUntil()
            && uiState().tryPressHotkey(kLayerDecreaseHotkeyIndex)) {
            uiState().queueLayerDelta(-1);
        }
        return true;
    }
    auto const loadProjectionHotkey = uiState().inputHotkey(kLoadProjectionHotkeyIndex);
    if (loadProjectionHotkey.key != 0 && virtualKey == loadProjectionHotkey.key
        && modifiers == loadProjectionHotkey.modifiers) {
        if (GetTickCount64() >= uiState().ignoreHotkeyUntil()
            && uiState().tryPressHotkey(kLoadProjectionHotkeyIndex)) {
            uiState().queueLoadProjection();
        }
        return true;
    }
    auto const closeProjectionHotkey = uiState().inputHotkey(kCloseProjectionHotkeyIndex);
    if (closeProjectionHotkey.key != 0 && virtualKey == closeProjectionHotkey.key
        && modifiers == closeProjectionHotkey.modifiers) {
        if (GetTickCount64() >= uiState().ignoreHotkeyUntil()
            && uiState().tryPressHotkey(kCloseProjectionHotkeyIndex)) {
            uiState().queueCloseProjection();
        }
        return true;
    }
    auto const toggleManualHotkey = uiState().inputHotkey(kToggleManualPlacementHotkeyIndex);
    if (toggleManualHotkey.key != 0 && virtualKey == toggleManualHotkey.key
        && modifiers == toggleManualHotkey.modifiers) {
        if (GetTickCount64() >= uiState().ignoreHotkeyUntil()
            && uiState().tryPressHotkey(kToggleManualPlacementHotkeyIndex)) {
            uiState().queueToggleManualPlacement();
        }
        return true;
    }
    return false;
}

bool handleGuiHotkeyKeyUp(unsigned int virtualKey) {
    if (virtualKey == VK_CONTROL || virtualKey == VK_LCONTROL || virtualKey == VK_RCONTROL) {
        uiState().setControlHeld(false);
        return false;
    }
    if (virtualKey == VK_MENU || virtualKey == VK_LMENU || virtualKey == VK_RMENU) {
        uiState().setAltHeld(false);
        return false;
    }
    if (virtualKey == VK_SHIFT || virtualKey == VK_LSHIFT || virtualKey == VK_RSHIFT) {
        uiState().setShiftHeld(false);
        return false;
    }

    return uiState().releaseHotkeysForKey(virtualKey, GetTickCount64());
}

bool handleProjectionOffsetWheel(short wheelDelta) {
    if (overlay::companion::isVisible() || isGuiVisible() || !uiState().altWheelOffsetEnabled() || !uiState().altHeld()
        || !detail::StructureSession::getInstance().hasLoaded()) {
        return false;
    }

    auto const view = capture::getClientViewSnapshot();
    if (!view) return false;

    auto const steps = static_cast<int>(wheelDelta) / WHEEL_DELTA;
    if (steps == 0) return false;
    auto const step = input::viewForwardStep(view->forward[0], view->forward[1], view->forward[2], steps);
    if (!step.valid) return false;
    uiState().queueOffsetDelta(step.dx, step.dy, step.dz);
    return true;
}

void resetHotkeyState() {
    uiState().resetHotkeyState();
}

void processPendingActions() {
    if (projection::consumeWorldExitRequest()) {
        place::resetWorldSession();
        capture::clear();
        resetWorldSession();
        showActionHint(
            i18n::Message{i18n::TextKey::ActionHintWorldExited},
            kProjectionLifecycleHintDurationMs
        );
        return;
    }

    schematic::processControl();
    processPreparedStructureLoad();

    auto& session = detail::StructureSession::getInstance();
    auto const pending = uiState().consumePendingHotkeyActions();
    auto const layerActionEnabled = pending.layerDelta != 0
        && session.transform().layerDisplayMode != LayerDisplayMode::All;
    bool changed = pending.offsetX != 0 || pending.offsetY != 0 || pending.offsetZ != 0 || layerActionEnabled;
    session.adjustOffsets(pending.offsetX, pending.offsetY, pending.offsetZ);
    if (layerActionEnabled) session.adjustDisplayLayer(pending.layerDelta);

    if (pending.loadProjection) {
        restoreSavedProjection();
        showActionHint(i18n::Message{i18n::TextKey::ActionHintLoadProjection});
    }
    if (pending.closeProjection) {
        clear();
        saveSettings();
        showActionHint(i18n::Message{i18n::TextKey::ActionHintCloseProjection});
    }
    if (pending.toggleManualPlacement) {
        bool const enable = !place::isManualMode();
        if (enable && !experimentalConsentGiven()) {
            showActionHint(i18n::Message{i18n::TextKey::HintAssistedDisabledByConsent});
        } else {
            place::setManualMode(enable);
            if (enable) {
                // Assisted-placement modes are mutually exclusive everywhere,
                // including when the mode is changed without opening the menu.
                place::setEnabled(false);
                place::setRangeEnabled(false);
                i18n::Message message{i18n::TextKey::HintModeEnabled};
                message.args[0] = i18n::tr(i18n::TextKey::ModeManual);
                showActionHint(message);
            } else {
                showActionHint(i18n::Message{i18n::TextKey::HintAssistedDisabled});
            }
        }
    }

    changed = pending.settingsSave || changed;
    if (changed) saveSettings();
}

void resetDimensionSession() {
    schematic::retireActiveProjection();
    uiState().cancelMenuRoutes();
    uiState().clearMaterialHud();
}

bool hasHudInfo() {
    if (!hudContextAvailable()) return false;
    if (!detail::StructureSession::getInstance().hasLoaded()) return false;
    // The material HUD renders independently of the projection HUD, so the
    // overlay must draw when it is enabled even if the projection HUD is off.
    if (materialHudEnabled()) return true;
    auto const hud = uiState().hud();
    return hud.hasVisibleFields();
}

namespace {
// Render-thread only. renderHud records its rect + corner each frame so
// renderMaterialHud (drawn right after, same frame) can stack clear of it when
// they share a corner. `frame` guards against stale reads.
struct ProjectionHudLayout {
    int   frame{-1};
    int   position{1};
    float topY{0.0f};
    float bottomY{0.0f};
};
ProjectionHudLayout  gProjectionHudLayout;
} // namespace

bool experimentalConsentGiven() {
    return uiState().experimentalConsentGiven();
}

void setExperimentalConsentGiven(bool given) {
    uiState().setExperimentalConsentGiven(given);
}

bool materialHudEnabled() {
    return uiState().materialHudEnabled();
}

void setMaterialHudEnabled(bool enabled) {
    uiState().setMaterialHudEnabled(enabled);
}

int materialHudPosition() {
    return uiState().materialHudPosition();
}

void setMaterialHudPosition(int position) {
    uiState().setMaterialHudPosition(position);
}

void showActionHint(i18n::Message message, std::uint64_t durationMs) {
    uiState().setActionHint(std::move(message), GetTickCount64() + durationMs);
}

i18n::Message makeLoadedStatusMessage(LoadedStructure const& loaded) {
    char sizeText[64]{};
    std::snprintf(
        sizeText, sizeof(sizeText), "%d x %d x %d", loaded.sizeX, loaded.sizeY, loaded.sizeZ
    );
    char blocksText[96]{};
    std::snprintf(
        blocksText, sizeof(blocksText), "%llu  |  Palette %llu",
        static_cast<unsigned long long>(loaded.renderBlocks.size()),
        static_cast<unsigned long long>(loaded.paletteEntries)
    );
    return {
        i18n::TextKey::StatusLoaded,
        {detail::pathToUtf8(loaded.sourcePath.filename()), sizeText, blocksText}
    };
}

bool actionHintActive() {
    return GetTickCount64() < uiState().actionHintExpiry();
}

void renderActionHint() {
    auto const now = GetTickCount64();
    auto const hint = uiState().actionHint();
    auto const expiry = hint.expiry;
    if (now >= expiry) return;
    if (hint.text.empty()) return;

    auto const remaining = expiry - now;
    float const alpha = remaining < 300 ? static_cast<float>(remaining) / 300.0f : 1.0f;
    auto const displaySize = ImGui::GetIO().DisplaySize;
    float const uiScale = std::clamp(
        std::min(displaySize.x / 1920.0f, displaySize.y / 1080.0f), 1.0f, 5.0f
    );
    auto const metrics = lholo::ui::calculateMetrics(displaySize, uiScale);
    lholo::ui::applyFluentTheme(metrics);

    // Centered horizontally, sitting just above the hotbar like JE's action bar.
    ImGui::SetNextWindowPos(
        ImVec2(displaySize.x * 0.5f, displaySize.y * kActionHintVerticalScreenRatio),
        ImGuiCond_Always,
        ImVec2(0.5f, 0.5f)
    );
    // Same neutral background as the HUDs: this bar used to carry its own
    // purple accent, which no longer matched either the Fluent theme or the
    // HUD next to it. Only the fade alpha is specific to the hint.
    ImGui::SetNextWindowBgAlpha(kOverlayWindowBgAlpha * alpha);
    auto hintText = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    hintText.w *= alpha;
    ImGui::PushStyleColor(ImGuiCol_Text, hintText);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, metrics.rounding * 0.6f);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding, ImVec2(metrics.sectionPadding, metrics.gap)
    );
    constexpr auto flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoNavInputs
        | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##LHoloActionHint", nullptr, flags)) {
        ImGui::TextUnformatted(hint.text.c_str());
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void renderHud() {
    if (isGuiVisible()) return;
    if (!hudContextAvailable()) return;
    auto const hud = uiState().hud();
    if (!hud.hasVisibleFields()) return;
    auto const showFileName = hud.showFileName;
    auto const showLayer = hud.showLayer;
    auto const showOverallProgress = hud.showOverallProgress;
    auto const showProgress = hud.showProgress;
    auto const showWrongState = hud.showWrongState;
    auto const showWrongType = hud.showWrongType;
    auto const showExtraBlocks = hud.showExtraBlocks;
    auto const showProjectedBlockName = hud.showProjectedBlockName;

    auto const sessionSnapshot = detail::StructureSession::getInstance().snapshot();
    if (!sessionSnapshot.loaded) return;
    auto const fileName = detail::pathToUtf8(sessionSnapshot.loaded->sourcePath.filename());
    auto const layerAxis = sessionSnapshot.transform.layerAxis;
    auto const layerMode = sessionSnapshot.transform.layerDisplayMode;
    auto const maxLayer = layerAxis == LayerAxis::Material
        ? std::max(0, static_cast<int>(sessionSnapshot.loaded->materialCount) - 1)
        : detail::maxLayerFor(*sessionSnapshot.loaded,layerAxis,sessionSnapshot.transform.rotation);

    auto const displaySize = ImGui::GetIO().DisplaySize;
    auto uiScale = hud.uiScale;
    if (uiScale <= 0.0f) {
        uiScale = std::clamp(
            std::min(displaySize.x / 1920.0f, displaySize.y / 1080.0f),
            1.0f,
            5.0f
        );
    }
    auto const customLayout=uiState().hudLayout(0);
    auto hudMetrics = lholo::ui::calculateMetrics(displaySize, uiScale);
    lholo::ui::applyFluentTheme(hudMetrics);
    // Per-window scaling retains the existing theme and shared font atlas.
    float const panelScale=lholo::ui::hudScaleMultiplier(hudMetrics.scale,customLayout);
    hudMetrics.scale*=panelScale;hudMetrics.gap*=panelScale;hudMetrics.outerPadding*=panelScale;
    hudMetrics.sectionPadding*=panelScale;hudMetrics.rounding*=panelScale;
    auto const currentLayer = std::clamp(
        sessionSnapshot.transform.displayLayer,
        0,
        maxLayer
    );

    auto const hudPosition = std::clamp(hud.position, 0, 3);
    auto const right = hudPosition >= 2;
    auto const bottom = (hudPosition & 1) != 0;
    auto const margin = hudMetrics.outerPadding;
    ImGui::SetNextWindowPos(
        customLayout.custom?ImVec2((customLayout.anchor%3)*.5f*displaySize.x+customLayout.x,(customLayout.anchor/3)*.5f*displaySize.y+customLayout.y):ImVec2(right ? displaySize.x - margin : margin, bottom ? displaySize.y - margin : margin),
        ImGuiCond_Always,
        customLayout.custom?ImVec2((customLayout.anchor%3)*.5f,(customLayout.anchor/3)*.5f):ImVec2(right ? 1.0f : 0.0f, bottom ? 1.0f : 0.0f)
    );
    ImGui::SetNextWindowBgAlpha(kOverlayWindowBgAlpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, hudMetrics.rounding * 0.7f);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(hudMetrics.sectionPadding, hudMetrics.gap)
    );
    constexpr auto flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoNavInputs
        | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoInputs;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(ImGui::GetStyle().ItemSpacing.x*panelScale,ImGui::GetStyle().ItemSpacing.y*panelScale));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(ImGui::GetStyle().FramePadding.x*panelScale,ImGui::GetStyle().FramePadding.y*panelScale));
    if (ImGui::Begin("##LHoloHud", nullptr, flags)) {
        ImGui::SetWindowFontScale(panelScale);
        if(customLayout.custom) {
            auto p=ImGui::GetWindowPos(),s=ImGui::GetWindowSize();
            ImGui::SetWindowPos({std::clamp(p.x,0.f,std::max(0.f,displaySize.x-s.x)),std::clamp(p.y,0.f,std::max(0.f,displaySize.y-s.y))});
        }
        if (showFileName) ImGui::Text(i18n::tr(i18n::TextKey::HudFileName), fileName.c_str());
        if (showLayer && layerAxis == LayerAxis::Material) {
            if (layerMode == LayerDisplayMode::All) {
                ImGui::TextUnformatted(i18n::tr(i18n::TextKey::HudMaterialFilterAll));
            } else if (layerMode == LayerDisplayMode::Single) {
                ImGui::Text(
                    i18n::tr(i18n::TextKey::HudMaterialFilterSingle),
                    currentLayer + 1,
                    maxLayer + 1
                );
            } else if (layerMode == LayerDisplayMode::UpToCurrent) {
                ImGui::Text(
                    i18n::tr(i18n::TextKey::HudMaterialFilterUpTo), currentLayer + 1
                );
            } else {
                ImGui::Text(
                    i18n::tr(i18n::TextKey::HudMaterialFilterRange),
                    currentLayer + 1,
                    maxLayer + 1
                );
            }
        } else if (showLayer && layerMode == LayerDisplayMode::All) {
            ImGui::TextUnformatted(i18n::tr(i18n::TextKey::HudRangeAll));
        } else if (showLayer && layerMode == LayerDisplayMode::Single) {
            ImGui::Text(
                i18n::tr(i18n::TextKey::HudCurrentLayer),
                currentLayer,
                maxLayer,
                layerAxisLabel(layerAxis)
            );
        } else if (showLayer && layerMode == LayerDisplayMode::UpToCurrent) {
            ImGui::Text(
                i18n::tr(i18n::TextKey::HudRangeFromZero),
                currentLayer,
                layerAxisLabel(layerAxis)
            );
        } else if (showLayer) {
            ImGui::Text(
                i18n::tr(i18n::TextKey::HudRangeBetween),
                currentLayer,
                maxLayer,
                layerAxisLabel(layerAxis)
            );
        }
        if(showLayer) {
            auto const& t=sessionSnapshot.transform;
            ImGui::Text(i18n::tr(i18n::TextKey::HudTransform),t.offsetX,t.offsetY,t.offsetZ,(t.rotation&3)*90,
                t.mirror==1?"X":t.mirror==2?"Z":"-");
        }
        auto const showAnyProgress = showOverallProgress || showProgress || showWrongState
            || showWrongType || showExtraBlocks;
        projection::BuildProgress progress{};
        if (showAnyProgress) progress = projection::getBuildProgress();
        if (showOverallProgress) {
            ImGui::Text(
                i18n::tr(i18n::TextKey::HudOverallProgress),
                static_cast<unsigned long long>(progress.placed),
                static_cast<unsigned long long>(progress.total)
            );
        }
        if (showProgress) {
            ImGui::Text(
                i18n::tr(i18n::TextKey::HudBuildProgress),
                static_cast<unsigned long long>(progress.visiblePlaced),
                static_cast<unsigned long long>(progress.visibleTotal)
            );
            auto const summary=lholo::ui::hudProgress(progress.visiblePlaced,progress.visibleTotal);
            if(summary.ratio) {
                char label[32]{};std::snprintf(label,sizeof(label),"%.1f%%",*summary.ratio*100.f);
                ImGui::ProgressBar(*summary.ratio,ImVec2(240*hudMetrics.scale,0),label);
                ImGui::Text(i18n::tr(i18n::TextKey::HudRemaining),static_cast<unsigned long long>(summary.remaining));
            } else ImGui::TextDisabled("%s",i18n::tr(i18n::TextKey::HudProgressUnknown));
        }
        if (showWrongState && progress.wrongState != 0) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.62f, 0.18f, 1.0f),
                i18n::tr(i18n::TextKey::HudWrongState),
                static_cast<unsigned long long>(progress.wrongState)
            );
        }
        if (showWrongType && progress.wrongType != 0) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.28f, 0.24f, 1.0f),
                i18n::tr(i18n::TextKey::HudWrongType),
                static_cast<unsigned long long>(progress.wrongType)
            );
        }
        if (showExtraBlocks && progress.extra != 0) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.30f, 0.90f, 1.0f),
                i18n::tr(i18n::TextKey::HudExtraBlocks),
                static_cast<unsigned long long>(progress.extra)
            );
        }
        auto const aimedProjectedBlock = place::getAimedProjectedBlockName();
        if (showProjectedBlockName && !aimedProjectedBlock.empty()) {
            ImGui::Text(
                i18n::tr(i18n::TextKey::HudProjectedBlock), aimedProjectedBlock.c_str()
            );
        }
        // Show which assisted-placement mode (if any) is currently on.
        char const* const placeMode = place::isManualMode()
            ? i18n::tr(i18n::TextKey::ModeManual)
            : place::isEnabled()
                ? i18n::tr(i18n::TextKey::ModeEasy)
                : place::isRangeEnabled() ? i18n::tr(i18n::TextKey::ModeRange) : nullptr;
        if (placeMode) {
            ImGui::TextColored(
                ImVec4(0.45f, 0.85f, 1.0f, 1.0f),
                i18n::tr(i18n::TextKey::HudPlacementMode),
                placeMode
            );
        }
        // Record our rect + corner so the material HUD (drawn right after) can
        // stack clear of us when it shares this corner, instead of overlapping.
        gProjectionHudLayout.frame = ImGui::GetFrameCount();
        gProjectionHudLayout.position = customLayout.custom?-1:hudPosition;
        gProjectionHudLayout.topY = ImGui::GetWindowPos().y;
        gProjectionHudLayout.bottomY = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
    }
    ImGui::End();
    ImGui::PopStyleVar(4);
}

void renderMaterialHud() {
    if (isGuiVisible()) return;
    if (!hudContextAvailable()) return;
    if (!materialHudEnabled()) return;
    auto const hud = uiState().hud();
    if (!detail::StructureSession::getInstance().hasLoaded()) return;
    // Both vectors come from one immutable owner so they stay index-aligned. The
    // tracker publishes only the current visible layer's missing materials;
    // this present thread performs no structure or inventory scans.
    auto const owner = uiState().materialHudView();
    detail::MaterialHudSnapshot const empty;
    auto const& snapshot = owner ? *owner : empty;
    auto const& materials = snapshot.requirements;
    auto const& available = snapshot.available;

    struct Row {
        char const*   name;
        std::uint64_t missing;
        int           stackSize;
    };
    std::vector<Row> missing;
    for (std::size_t index = 0; index < materials.size(); ++index) {
        auto const need = materials[index].count;
        auto const have = index < available.size() ? std::max(0,available[index]) : 0;
        auto const miss = static_cast<std::uint64_t>(have) >= need
            ? 0ULL : need - static_cast<std::uint64_t>(have);
        if (miss > 0) {
            missing.push_back({
                lholo::ui::materialDisplayName(
                    materials[index].displayName, materials[index].nameKey
                ),
                miss,
                materials[index].stackSize
            });
        }
    }
    std::sort(missing.begin(), missing.end(), [](Row const& a, Row const& b) {
        return a.missing > b.missing;
    });

    auto const displaySize = ImGui::GetIO().DisplaySize;
    float uiScale = hud.uiScale;
    if (uiScale <= 0.0f) {
        uiScale = std::clamp(std::min(displaySize.x / 1920.0f, displaySize.y / 1080.0f), 1.0f, 5.0f);
    }
    auto const customLayout=uiState().hudLayout(1);
    auto metrics = lholo::ui::calculateMetrics(displaySize, uiScale);
    lholo::ui::applyFluentTheme(metrics);
    // Per-window scaling retains the existing theme and shared font atlas.
    float const panelScale=lholo::ui::hudScaleMultiplier(metrics.scale,customLayout);
    metrics.scale*=panelScale;metrics.gap*=panelScale;metrics.outerPadding*=panelScale;
    metrics.sectionPadding*=panelScale;metrics.rounding*=panelScale;
    auto const margin = metrics.outerPadding;
    auto const position = std::clamp(materialHudPosition(), 0, 3);
    bool const right = position >= 2;
    bool const bottom = (position & 1) != 0;
    float anchorX = right ? displaySize.x - margin : margin;
    float anchorY = bottom ? displaySize.y - margin : margin;
    // When the projection HUD occupies the same corner this frame, stack clear
    // of it — above it for a bottom corner, below it for a top corner.
    if (gProjectionHudLayout.frame == ImGui::GetFrameCount()
        && gProjectionHudLayout.position == position) {
        if (bottom) {
            anchorY = std::min(anchorY, gProjectionHudLayout.topY - metrics.gap);
        } else {
            anchorY = std::max(anchorY, gProjectionHudLayout.bottomY + metrics.gap);
        }
    }
    ImGui::SetNextWindowPos(
        customLayout.custom?ImVec2((customLayout.anchor%3)*.5f*displaySize.x+customLayout.x,(customLayout.anchor/3)*.5f*displaySize.y+customLayout.y):ImVec2(anchorX, anchorY), ImGuiCond_Always, customLayout.custom?ImVec2((customLayout.anchor%3)*.5f,(customLayout.anchor/3)*.5f):ImVec2(right ? 1.0f : 0.0f, bottom ? 1.0f : 0.0f)
    );
    ImGui::SetNextWindowBgAlpha(kOverlayWindowBgAlpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, metrics.rounding * 0.7f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(metrics.sectionPadding, metrics.gap));
    constexpr auto flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoNavInputs
        | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoInputs;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(ImGui::GetStyle().ItemSpacing.x*panelScale,ImGui::GetStyle().ItemSpacing.y*panelScale));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(ImGui::GetStyle().FramePadding.x*panelScale,ImGui::GetStyle().FramePadding.y*panelScale));
    if (ImGui::Begin("##LHoloMaterialHud", nullptr, flags)) {
        ImGui::SetWindowFontScale(panelScale);
        if(customLayout.custom) {
            auto p=ImGui::GetWindowPos(),s=ImGui::GetWindowSize();
            ImGui::SetWindowPos({std::clamp(p.x,0.f,std::max(0.f,displaySize.x-s.x)),std::clamp(p.y,0.f,std::max(0.f,displaySize.y-s.y))});
        }
        ImGui::TextUnformatted(i18n::tr(i18n::TextKey::MaterialHudTitle));
        ImGui::Separator();
        if (!snapshot.ready) {
            ImGui::TextDisabled("%s", i18n::tr(i18n::TextKey::MaterialHudScanning));
        } else if (missing.empty()) {
            ImGui::TextColored(
                ImVec4(0.55f, 0.85f, 0.40f, 1.0f),
                "%s",
                i18n::tr(i18n::TextKey::MaterialHudComplete)
            );
        } else {
            lholo::ui::MissingTotal total;for(auto const& row:missing)total.add(row.missing);
            if(total.exact)ImGui::Text(i18n::tr(i18n::TextKey::MaterialHudSummary),missing.size(),static_cast<unsigned long long>(total.value));
            else ImGui::Text(i18n::tr(i18n::TextKey::MaterialHudTypes),missing.size());
            constexpr std::size_t kMaxRows = 14;
            for (std::size_t index = 0; index < missing.size() && index < kMaxRows; ++index) {
                auto const& row = missing[index];
                // JE Litematica style: name then the missing amount broken into
                // stacks, e.g. "白色玻璃  111 (1 x 64 + 47)".
                ImGui::TextColored(
                    ImVec4(1.0f, 0.62f, 0.20f, 1.0f), "%s  %s",
                    row.name,
                    lholo::ui::formatStackCount(row.missing, row.stackSize).c_str()
                );
            }
            if (missing.size() > kMaxRows) {
                ImGui::TextDisabled(
                    i18n::tr(i18n::TextKey::MaterialHudMore), missing.size() - kMaxRows
                );
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(4);
}

void renderGui() {
    lholo::ui::renderStructureMenu();
}

void loadSettings() {
    auto const path = settingsPath();
    try {
        auto& session = detail::StructureSession::getInstance();
        lholo::settings::Settings settings;
        if (!lholo::settings::loadSettingsFile(path, settings)) {
            saveSettings();
            return;
        }
        if (!i18n::setLanguageByCode(settings.language)) {
            logger().warn(
                "Invalid interface language '{}'; using '{}'",
                settings.language,
                i18n::kDefaultLanguageCode
            );
        }
        session.setLastPath(settings.lastStructurePath);
        uiState().setUiScale(std::clamp(settings.uiScale, 0.0f, 5.0f));
        projection::setOpacity(settings.opacity);
        projection::setCorrectionFillOpacity(settings.correctionFillOpacity);
        projection::setCorrectionOutlineOpacity(settings.correctionOutlineOpacity);
        projection::setComparisonStrength(settings.comparisonStrength);
        projection::setCorrectionOutlineWidth(settings.correctionOutlineWidth);
        projection::setStructureBoundsEnabled(settings.structureBoundsEnabled);
        projection::setCorrectionSeeThrough(settings.correctionSeeThrough);
        projection::setMissingSeeThrough(settings.missingSeeThrough);
        setExperimentalConsentGiven(settings.experimentalConsent);
        setMaterialHudEnabled(settings.materialHudEnabled);
        setMaterialHudPosition(settings.materialHudPosition);
        // Transform and layer state are session-local. Only the explicit
        // "restore last projection" record below is persisted.
        session.resetTransform();
        auto hud = uiState().hud();
        hud.enabled = settings.hudEnabled;
        hud.showFileName = settings.hudShowFileName;
        hud.showLayer = settings.hudShowLayer;
        hud.showOverallProgress = settings.hudShowOverallProgress;
        hud.showProgress = settings.hudShowProgress;
        hud.showWrongState = settings.hudShowWrongState;
        hud.showWrongType = settings.hudShowWrongType;
        hud.showExtraBlocks = settings.hudShowExtraBlocks;
        hud.showProjectedBlockName = settings.hudShowProjectedBlockName;
        hud.position = std::clamp(settings.hudPosition, 0, 3);
        uiState().applyHud(hud);
        // Assisted-placement modes are intentionally session-only. Ignore
        // legacy persisted values and always begin a new game session disabled.
        place::setEnabled(false);
        place::setManualMode(false);
        place::setRangeEnabled(false);
        place::setManualPlacementAllowedItems(settings.manualPlacementAllowedItems);
        place::setPlacementRadius(std::clamp(settings.placementRadius, 1, 4));
        place::setAutoPlacementBreakCooldownSeconds(
            std::clamp(settings.autoPlacementBreakCooldownSeconds, 0, 60)
        );
        uiState().setHotkey(
            kGuiHotkeyIndex,
            std::clamp(settings.guiHotkey, 0, 255),
            std::clamp(settings.guiHotkeyModifiers, 0, 7)
        );
        uiState().setAltWheelOffsetEnabled(settings.altWheelOffsetEnabled);
        for(unsigned i=0;i<2;++i)uiState().setHudLayout(i,settings.hudLayouts[i]);
        uiState().setHotkey(
            kLayerIncreaseHotkeyIndex,
            std::clamp(settings.layerIncreaseHotkey, 0, 255),
            std::clamp(settings.layerIncreaseHotkeyModifiers, 0, 7)
        );
        uiState().setHotkey(
            kLayerDecreaseHotkeyIndex,
            std::clamp(settings.layerDecreaseHotkey, 0, 255),
            std::clamp(settings.layerDecreaseHotkeyModifiers, 0, 7)
        );
        for (std::size_t index = 0; index < input::kMoveHotkeyCount; ++index) {
            uiState().setHotkey(
                index + input::kMoveHotkeyFirst,
                std::clamp(settings.moveHotkeys[index], 0, 255),
                std::clamp(settings.moveHotkeyModifiers[index], 0, 7)
            );
        }
        uiState().setHotkey(
            kLoadProjectionHotkeyIndex,
            std::clamp(settings.loadProjectionHotkey, 0, 255),
            std::clamp(settings.loadProjectionHotkeyModifiers, 0, 7)
        );
        uiState().setHotkey(
            kCloseProjectionHotkeyIndex,
            std::clamp(settings.closeProjectionHotkey, 0, 255),
            std::clamp(settings.closeProjectionHotkeyModifiers, 0, 7)
        );
        uiState().setHotkey(
            kToggleManualPlacementHotkeyIndex,
            std::clamp(settings.toggleManualHotkey, 0, 255),
            std::clamp(settings.toggleManualHotkeyModifiers, 0, 7)
        );
        for(std::size_t index=0;index<input::kDirectMenuHotkeyCount;++index)
            uiState().setHotkey(input::kDirectMenuHotkeyFirst+index,
                std::clamp(settings.directMenuHotkeys[index],0,255),
                std::clamp(settings.directMenuHotkeyModifiers[index],0,7));
        session.setSavedProjection({
            settings.hasSavedProjection,
            settings.savedAnchorX,
            settings.savedAnchorY,
            settings.savedAnchorZ,
            {
                settings.savedRotation,
                std::clamp(settings.savedMirror, 0, 2),
                settings.savedOffsetX,
                settings.savedOffsetY,
                settings.savedOffsetZ,
                layerDisplayModeFromInt(settings.savedLayerDisplayMode),
                settings.savedDisplayLayer,
                layerAxisFromInt(settings.savedLayerAxis),
                settings.savedVisible, settings.savedCountExtras
            },
            settings.savedStructurePath
        });
        logger().info("Loaded projection settings from {}", path.string());
    } catch (std::exception const& exception) {
        logger().error("Could not load projection settings {}: {}", path.string(), exception.what());
    }
}

void saveSettings() {
    schematic::rememberSelectedTransform();
    auto const path = settingsPath();
    try {
        auto& session = detail::StructureSession::getInstance();
        // Only an active projection may update its restore snapshot. At
        // startup the session-local transform/layer values intentionally
        // reset to defaults; copying those values before the user restores
        // a structure would silently destroy the saved state.
        session.refreshSavedTransformIfActive();
        auto const sessionSnapshot = session.snapshot();
        auto const hud = uiState().hud();
        lholo::settings::Settings settings;
        settings.lastStructurePath = sessionSnapshot.lastPath;
        settings.language = std::string{i18n::languageCode(i18n::language())};
        settings.uiScale = hud.uiScale;
        settings.opacity = projection::getOpacity();
        settings.correctionFillOpacity = projection::getCorrectionFillOpacity();
        settings.correctionOutlineOpacity = projection::getCorrectionOutlineOpacity();
        settings.comparisonStrength = projection::getComparisonStrength();
        settings.correctionOutlineWidth = projection::getCorrectionOutlineWidth();
        settings.structureBoundsEnabled = projection::getStructureBoundsEnabled();
        settings.correctionSeeThrough = projection::getCorrectionSeeThrough();
        settings.missingSeeThrough = projection::getMissingSeeThrough();
        settings.experimentalConsent = experimentalConsentGiven();
        settings.materialHudEnabled = materialHudEnabled();
        settings.materialHudPosition = materialHudPosition();
        settings.manualPlacementAllowedItems = place::getManualPlacementAllowedItems();
        settings.placementRadius = place::getPlacementRadius();
        settings.autoPlacementBreakCooldownSeconds
            = place::getAutoPlacementBreakCooldownSeconds();
        settings.hudEnabled = hud.enabled;
        settings.hudShowFileName = hud.showFileName;
        settings.hudShowLayer = hud.showLayer;
        settings.hudShowOverallProgress = hud.showOverallProgress;
        settings.hudShowProgress = hud.showProgress;
        settings.hudShowWrongState = hud.showWrongState;
        settings.hudShowWrongType = hud.showWrongType;
        settings.hudShowExtraBlocks = hud.showExtraBlocks;
        settings.hudShowProjectedBlockName = hud.showProjectedBlockName;
        settings.hudPosition = hud.position;
        for(unsigned i=0;i<2;++i)settings.hudLayouts[i]=uiState().hudLayout(i);
        auto const guiHotkey = uiState().hotkey(kGuiHotkeyIndex);
        auto const layerIncreaseHotkey = uiState().hotkey(kLayerIncreaseHotkeyIndex);
        auto const layerDecreaseHotkey = uiState().hotkey(kLayerDecreaseHotkeyIndex);
        settings.guiHotkey = guiHotkey.key;
        settings.guiHotkeyModifiers = guiHotkey.modifiers;
        settings.layerIncreaseHotkey = layerIncreaseHotkey.key;
        settings.layerDecreaseHotkey = layerDecreaseHotkey.key;
        settings.layerIncreaseHotkeyModifiers = layerIncreaseHotkey.modifiers;
        settings.layerDecreaseHotkeyModifiers = layerDecreaseHotkey.modifiers;
        for (std::size_t index = 0; index < settings.moveHotkeys.size(); ++index) {
            auto const moveHotkey = uiState().hotkey(index + input::kMoveHotkeyFirst);
            settings.moveHotkeys[index] = moveHotkey.key;
            settings.moveHotkeyModifiers[index] = moveHotkey.modifiers;
        }
        auto const loadProjectionHotkey = uiState().hotkey(kLoadProjectionHotkeyIndex);
        auto const closeProjectionHotkey = uiState().hotkey(kCloseProjectionHotkeyIndex);
        auto const toggleManualHotkey = uiState().hotkey(kToggleManualPlacementHotkeyIndex);
        settings.loadProjectionHotkey = loadProjectionHotkey.key;
        settings.loadProjectionHotkeyModifiers = loadProjectionHotkey.modifiers;
        settings.closeProjectionHotkey = closeProjectionHotkey.key;
        settings.closeProjectionHotkeyModifiers = closeProjectionHotkey.modifiers;
        settings.toggleManualHotkey = toggleManualHotkey.key;
        settings.toggleManualHotkeyModifiers = toggleManualHotkey.modifiers;
        for(std::size_t index=0;index<input::kDirectMenuHotkeyCount;++index) {
            auto const hotkey=uiState().hotkey(input::kDirectMenuHotkeyFirst+index);
            settings.directMenuHotkeys[index]=hotkey.key;
            settings.directMenuHotkeyModifiers[index]=hotkey.modifiers;
        }
        settings.altWheelOffsetEnabled = uiState().altWheelOffsetEnabled();
        settings.hasSavedProjection = sessionSnapshot.saved.available;
        settings.savedAnchorX = sessionSnapshot.saved.anchorX;
        settings.savedAnchorY = sessionSnapshot.saved.anchorY;
        settings.savedAnchorZ = sessionSnapshot.saved.anchorZ;
        settings.savedRotation = sessionSnapshot.saved.transform.rotation;
        settings.savedMirror = sessionSnapshot.saved.transform.mirror;
        settings.savedVisible = sessionSnapshot.saved.transform.visible;
        settings.savedCountExtras = sessionSnapshot.saved.transform.countExtras;
        settings.savedOffsetX = sessionSnapshot.saved.transform.offsetX;
        settings.savedOffsetY = sessionSnapshot.saved.transform.offsetY;
        settings.savedOffsetZ = sessionSnapshot.saved.transform.offsetZ;
        settings.savedLayerDisplayMode = toInt(sessionSnapshot.saved.transform.layerDisplayMode);
        settings.savedDisplayLayer = sessionSnapshot.saved.transform.displayLayer;
        settings.savedLayerAxis = toInt(sessionSnapshot.saved.transform.layerAxis);
        settings.savedStructurePath = sessionSnapshot.saved.structurePath;
        lholo::settings::saveSettingsFile(path, settings);
    } catch (std::exception const& exception) {
        logger().error("Could not save projection settings {}: {}", path.string(), exception.what());
    }
}

std::shared_ptr<LoadedStructure const> getLoaded() {
    return detail::StructureSession::getInstance().loaded();
}

int getRotationQuarterTurns() {
    return detail::StructureSession::getInstance().transform().rotation;
}

int getMirrorMode() {
    return std::clamp(detail::StructureSession::getInstance().transform().mirror, 0, 2);
}

int getOffsetX() { return detail::StructureSession::getInstance().transform().offsetX; }
int getOffsetY() { return detail::StructureSession::getInstance().transform().offsetY; }
int getOffsetZ() { return detail::StructureSession::getInstance().transform().offsetZ; }
LayerDisplayMode getLayerDisplayMode() {
    return detail::StructureSession::getInstance().transform().layerDisplayMode;
}
int getDisplayLayer() { return detail::StructureSession::getInstance().transform().displayLayer; }
LayerAxis getLayerAxis() { return detail::StructureSession::getInstance().transform().layerAxis; }

void recordProjectionAnchor(int x, int y, int z) {
    detail::StructureSession::getInstance().recordProjectionAnchor(x, y, z);
    saveSettings();
}
bool recordProjectionAnchor(std::shared_ptr<LoadedStructure const> const& expected,std::uint64_t generation,int x,int y,int z) {
    if(!detail::StructureSession::getInstance().recordProjectionAnchor(expected,generation,x,y,z))return false;
    saveSettings();return true;
}

// Hotbar lock for the Alt+wheel projection offset: engages only while the
// gesture is enabled, a projection is loaded AND the Alt key is held.
// Deliberately reads the same event-tracked Alt state the wheel handler uses,
// so the lock and the projection move engage under exactly the same condition
// and cost nothing while idle. The mouse-input hook (input/MenuInputGuard)
// consults this to suppress wheel-driven hotbar changes at the Bedrock input
// boundary, so disabling the gesture releases the wheel immediately instead of
// waiting for a key-up.
bool scrollLockActive() {
    return getLoaded() != nullptr && uiState().altWheelOffsetEnabled() && uiState().altHeld();
}

void requestStructureFileLoad(std::string pathText) {
    auto& session = detail::StructureSession::getInstance();
    if (pathText.empty()) {
        session.setStatus(i18n::Message{i18n::TextKey::StatusPathEmpty});
        return;
    }

    auto const path = detail::pathFromUtf8(pathText);
    auto const request = schematic::beginProjectionRequest();
    bool handedOff{};
    app::ScopeExit retireFailed([&]() noexcept {if(!handedOff)schematic::failProjectionRequest(request);});
    auto const ticket = gLoadIntent.begin();
    gQueuedStructureLoad.reset();
    if (detail::supportsAsyncStructurePreparation(path)) {
        handedOff=startPreparedStructureLoad(
            PendingStructureLoadMode::Normal,
            std::move(pathText),
            ticket,
            {},
            request
        );
        return;
    }

    std::string error;
    auto loaded = detail::loadStructureFile(path, error);
    if (!loaded) {
        gLoadIntent.applyIfCurrent(ticket, [&] {
            session.setStatus(i18n::Message{i18n::TextKey::StatusLoadFailed, {error}});
        });
        logger().error("Could not load structure {}: {}", pathText, error);
        return;
    }
    gLoadIntent.applyIfCurrent(ticket, [&] { commitNormalStructureLoad(std::move(loaded), pathText, request);handedOff=true; });
}

void restoreSavedProjection(std::uint64_t placementId) {
    auto& session = detail::StructureSession::getInstance();
    auto const saved = session.savedProjection();
    auto const& savedPath = saved.structurePath;

    auto const path = detail::pathFromUtf8(savedPath);
    auto const request = schematic::beginProjectionRequest(placementId);
    bool handedOff{};
    app::ScopeExit retireFailed([&]() noexcept {if(!handedOff)schematic::failProjectionRequest(request);});
    auto const ticket = gLoadIntent.begin();
    gQueuedStructureLoad.reset();
    if (detail::supportsAsyncStructurePreparation(path)) {
        handedOff=startPreparedStructureLoad(
            PendingStructureLoadMode::Restore,
            savedPath,
            ticket,
            saved,
            request
        );
        return;
    }

    std::string error;
    auto loaded = detail::loadStructureFile(path, error);
    if (!loaded) {
        gLoadIntent.applyIfCurrent(ticket, [&] {
            session.setStatus(i18n::Message{i18n::TextKey::StatusRestoreFailed, {error}});
        });
        logger().error("Could not restore structure {}: {}", savedPath, error);
        return;
    }
    gLoadIntent.applyIfCurrent(ticket, [&] { commitRestoredStructureLoad(std::move(loaded), saved, request);handedOff=true; });
}

namespace {

void clearProjectionSession(i18n::Message status) {
    // Withdraw the requested structure before waiting for the mesh worker.
    // Otherwise the render hook can observe the old loaded structure in the gap after
    // projection::disable() and immediately enable the projection again.
    gLoadIntent.invalidateAndApply([&] {
        projection::cancelNextStructureAnchorRequest();
        detail::StructureSession::getInstance().clearLoaded(std::move(status));
    });

    // Drain the active projection before dropping conversion lookup caches.
    // The cached Block pointers refer to the engine registry, not cache-owned
    // blocks; keeping that ownership distinction explicit matters at teardown.
    projection::disable();
    resetJavaBlockMappingCache();
}

} // namespace

void resetWorldSession() {
    clearProjectionSession(i18n::Message{i18n::TextKey::StatusWorldExited});
    uiState().resetWorldSession();
    schematic::reset();
}

void shutdownPendingStructureLoad() {
    gLoadIntent.begin();
    gQueuedStructureLoad.reset();
    if (!gPendingStructureLoad) return;

    auto pending = app::takePendingValue(gPendingStructureLoad);
    app::invokeNativeCallback([&] {
        if (pending.future.valid()) {
            pending.future.wait();
            auto const result = pending.future.get();
            if (!result.error.empty()) {
                app::reportNativeCallbackFailure("structure load shutdown", result.error.c_str());
            }
        }
    }, [](char const* reason) noexcept {
        app::reportNativeCallbackFailure("structure load shutdown", reason);
    });
}

void clear(bool preserveQueuedVerification) {
    schematic::retireActiveProjection(preserveQueuedVerification);
    clearProjectionSession(i18n::Message{i18n::TextKey::StatusProjectionClosed});
    uiState().clearMaterials();
}

} // namespace lholo::structure
