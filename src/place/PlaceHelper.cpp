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

#include "place/PlaceHelper.h"

#include "app/HookLifecycle.h"
#include "app/NativeCallbackBoundary.h"
#include "i18n/Message.h"
#include "place/PlacementExecutor.h"
#include "place/PlacementState.h"

#include "plugin/LHolo.h"
#include "structure/MaterialTracker.h"
#include "structure/capture/StructureCapture.h"
#include "structure/StructureLoader.h"

#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/Bedrock.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/HandSlot.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Tick.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>

namespace lholo::place {
namespace {

auto& placementState() {
    return detail::PlacementState::getInstance();
}

auto& logger() {
    return LHolo::getInstance().getSelf().getLogger();
}

struct PlaceHookStatus {
    bool tick{};
    bool manualStart{};
    bool manualUseItem{};
    bool manualStop{};
    bool manualBuild{};
};

PlaceHookStatus gHookStatus;
std::atomic_bool gPlacementHooksReady{};

LL_TYPE_INSTANCE_HOOK(
    LocalPlayerEasyPlaceHook,
    ll::memory::HookPriority::Normal,
    LocalPlayer,
    &LocalPlayer::$tickWorld,
    void,
    ::Tick const& currentTick
) {
    app::hook_lifecycle::DetourGuard guard;
    if (!guard || !gPlacementHooksReady.load(std::memory_order_acquire)) {
        origin(currentTick);
        return;
    }
    app::invokeNativeCallback([&] { structure::capture::tick(*this); },
        [](char const* reason) noexcept { app::reportNativeCallbackFailure("capture game tick", reason); });
    app::invokeNativeCallback([&] {
        structure::detail::tickMaterialTracker(*this);
        // Physical mouse state belongs to the game-input Hook boundary. The
        // executor consumes only the resulting logical press state.
        if (placementState().manualMode()
            && (GetAsyncKeyState(VK_RBUTTON) & 0x8000) == 0) {
            placementState().releaseManualPress();
        }
        detail::tickEasyPlace(*this);
    }, [](char const* reason) noexcept {
        placementState().cancelManualPress();
        app::reportNativeCallbackFailure("game tick", reason);
    });
    origin(currentTick);
}

// Returns true when manual mode is on and `gm` belongs to the local player, i.e.
// this is the client-side right-click we should take over. The local-player
// check is essential: the server processes LHolo's own placement through these
// same functions on the ServerPlayer, and that must not be suppressed.
bool isLocalManualBuild(GameMode& gm) {
    if (!gPlacementHooksReady.load(std::memory_order_acquire)) return false;
    if (!placementState().manualMode()) return false;
    auto client = ll::service::getClientInstance();
    auto* localPlayer = client ? client->getLocalPlayer() : nullptr;
    return localPlayer && &gm.mPlayer == static_cast<Player*>(localPlayer);
}

void cancelPendingManualPress() {
    placementState().cancelManualPress();
}

bool aimedBlockAcceptsRightClick(GameMode& gm, BlockPos const& pos) {
    // Defer to Bedrock's official interaction classification so new vanilla and
    // custom interactive blocks do not require an LHolo name allow-list.
    return gm.mPlayer.getDimensionBlockSource().getBlock(pos).getBlockType().isInteractiveBlock();
}

// Manual-mode press edge. If the aimed block is interactive (chest, repeater,
// ...) we let vanilla open/use it. Otherwise we take the right button over: on a
// projection target LHolo places it (from tickEasyPlace), and off-target we block
// the accidental placement and show a one-shot JE-style hint. The vanilla build
// is allowed through only for an explicitly exempt held item.
LL_TYPE_INSTANCE_HOOK(
    GameModeStartBuildHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$startBuildBlock,
    void,
    ::BlockPos const& pos,
    uchar             face,
    ::HandSlot        handSlot
) {
    app::hook_lifecycle::DetourGuard guard;
    if (!guard) {
        origin(pos, face, handSlot);
        return;
    }
    auto const inputEpoch = placementState().manualInputEpoch();
    bool handled{};
    auto const succeeded = app::invokeNativeCallback([&] {
      if (isLocalManualBuild(*this)) {
        if (handSlot == HandSlot::Mainhand && isManualPlacementHeldItemAllowed(mPlayer)) {
            cancelPendingManualPress();
            return;
        }
        auto const targetStatus = detail::manualTargetStatusUnderCrosshair(mPlayer);
        // A ready projection target takes precedence over the interaction of the
        // real support block behind it. This is essential for hoppers/torches
        // placed against containers, droppers, dispensers and other interactive
        // supports: vanilla must not open/use the support instead of placing.
        if (targetStatus == detail::ManualTargetStatus::Ready) {
            (void)placementState().beginManualPress(GetTickCount64(), inputEpoch);
        } else if (aimedBlockAcceptsRightClick(*this, pos)) {
            cancelPendingManualPress();
            return; // no ready ghost: preserve vanilla interaction
        } else if (targetStatus == detail::ManualTargetStatus::MissingMaterial) {
            cancelPendingManualPress();
            structure::showActionHint(i18n::Message{i18n::TextKey::ActionHintNoMatchingItem});
        } else {
            cancelPendingManualPress();
            structure::showActionHint(
                i18n::Message{i18n::TextKey::ActionHintManualModeBlocked}
            );
        }
        handled = true; // LHolo owns this press; vanilla places nothing.
      }
    }, [](char const* reason) noexcept {
        cancelPendingManualPress();
        app::reportNativeCallbackFailure("manual build press", reason);
    });
    if (succeeded && handled) return;
    // Keep the native origin outside the LHolo exception boundary: an engine
    // exception must never cause the same native action to be replayed.
    origin(pos, face, handSlot);
}

// Right-clicking a floating projection targets air, so Bedrock calls useItem
// instead of startBuildBlock. Capture it only when a floating projection cell is
// under the crosshair; otherwise let vanilla use the item (eat, etc.).
LL_TYPE_INSTANCE_HOOK(
    GameModeUseItemHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$useItem,
    bool,
    ::ItemStack& item,
    ::HandSlot   handSlot
) {
    app::hook_lifecycle::DetourGuard guard;
    if (!guard) return origin(item, handSlot);
    auto const inputEpoch = placementState().manualInputEpoch();
    bool handled{};
    auto const succeeded = app::invokeNativeCallback([&] {
      if (isLocalManualBuild(*this)) {
        if (handSlot == HandSlot::Mainhand && isManualPlacementItemAllowed(item)) {
            cancelPendingManualPress();
            return;
        }
        auto const targetStatus = detail::manualTargetStatusUnderCrosshair(mPlayer);
        if (targetStatus == detail::ManualTargetStatus::None) {
            cancelPendingManualPress();
            return;
        }
        if (targetStatus == detail::ManualTargetStatus::Ready) {
            // Mark the button held so holding right-click over a floating
            // projection keeps placing (the tick's typematic repeat). The tick
            // clears the hold when the right button is actually released.
            (void)placementState().beginManualPress(GetTickCount64(), inputEpoch);
        } else {
            placementState().cancelManualPress();
            structure::showActionHint(
                i18n::Message{i18n::TextKey::ActionHintNoMatchingItem}
            );
        }
        handled = true;
      }
    }, [](char const* reason) noexcept {
        cancelPendingManualPress();
        app::reportNativeCallbackFailure("manual item use", reason);
    });
    if (succeeded && handled) return false;
    return origin(item, handSlot);
}

// Manual-mode release edge: stop the repeat when the button is let go.
LL_TYPE_INSTANCE_HOOK(
    GameModeStopBuildHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$stopBuildBlock,
    void
) {
    app::hook_lifecycle::DetourGuard guard;
    if (!guard) {
        origin();
        return;
    }
    app::invokeNativeCallback([&] {
        if (isLocalManualBuild(*this)) placementState().releaseManualPress();
    }, [](char const* reason) noexcept {
        app::reportNativeCallbackFailure("manual build release", reason);
    });
    origin();
}

// GameMode::buildBlock is the vanilla continuous-build placement. In manual mode
// we suppress it unless the actual held item is explicitly exempt. Otherwise the
// interact for an interactive block already happened there, and this only ever
// carries a block PLACEMENT, which manual mode blocks. No hint here — the press
// edge shows it once, so holding the button never spams the notification.
LL_TYPE_INSTANCE_HOOK(
    GameModeBuildBlockHook,
    ll::memory::HookPriority::Normal,
    GameMode,
    &GameMode::$buildBlock,
    bool,
    ::BlockPos const& pos,
    uchar             face,
    ::HandSlot        handSlot,
    bool const        isSimTick
) {
    app::hook_lifecycle::DetourGuard guard;
    if (!guard) return origin(pos, face, handSlot, isSimTick);
    bool handled{};
    auto const succeeded = app::invokeNativeCallback([&] {
      if (isLocalManualBuild(*this)) {
        if (handSlot == HandSlot::Mainhand && isManualPlacementHeldItemAllowed(mPlayer)) {
            cancelPendingManualPress();
            return;
        }
        handled = true;
      }
    }, [](char const* reason) noexcept {
        cancelPendingManualPress();
        app::reportNativeCallbackFailure("manual continuous build", reason);
    });
    if (succeeded && handled) return false;
    return origin(pos, face, handSlot, isSimTick);
}

} // namespace

void setEnabled(bool enabled) {
    if (enabled) {
        logger().info("Easy-place enabled");
    } else {
        logger().info("Easy-place disabled");
    }
    placementState().setEnabled(enabled);
}

bool isEnabled() {
    return placementState().enabled();
}

void setRangeEnabled(bool enabled) {
    if (enabled) {
        logger().info("Range placement enabled (radius {})", placementState().radius());
    } else {
        logger().info("Range placement disabled");
    }
    placementState().setRangeEnabled(enabled);
}

bool isRangeEnabled() {
    return placementState().rangeEnabled();
}

void setPlacementRadius(int radius) {
    placementState().setRadius(std::clamp(radius, 1, 4));
}

int getPlacementRadius() {
    return placementState().radius();
}

void setAutoPlacementBreakCooldownSeconds(int seconds) {
    placementState().setAutoPlacementBreakCooldownSeconds(std::clamp(seconds, 0, 60));
}

int getAutoPlacementBreakCooldownSeconds() {
    return placementState().autoPlacementBreakCooldownSeconds();
}

void setManualMode(bool manual) {
    placementState().setManualMode(manual);
}

bool isManualMode() {
    return placementState().manualMode();
}

PlacementModes getPlacementModes() {
    return placementState().modes();
}

bool applyPlacementModes(PlacementModes const& modes) {
    auto const previous = placementState().modes();
    if (!placementState().applyModes(modes)) return false;
    if (previous.enabled != modes.enabled) {
        logger().info("Easy-place {}", modes.enabled ? "enabled" : "disabled");
    }
    if (previous.range != modes.range) {
        if (modes.range) logger().info("Range placement enabled (radius {})", placementState().radius());
        else logger().info("Range placement disabled");
    }
    return true;
}

std::vector<std::string> getManualPlacementAllowedItems() {
    return placementState().manualPlacementAllowedItems();
}

bool setManualPlacementAllowedItems(std::vector<std::string> const& items) {
    return placementState().setManualPlacementAllowedItems(items);
}

bool isManualPlacementItemAllowed(ItemStack const& item) {
    return !item.isNull() && placementState().manualPlacementItemAllowed(item.getTypeName());
}

bool isManualPlacementHeldItemAllowed(Player& player) {
    return isManualPlacementItemAllowed(player.getInventory().getItem(player.getSelectedItemSlot()));
}

std::string getAimedProjectedBlockName() {
    return placementState().aimedProjectedBlockName();
}

void resetDimensionSession() {
    placementState().resetDimensionSession();
}

void resetWorldSession() {
    placementState().resetWorldSession();
}

bool installHook() {
    gPlacementHooksReady.store(false, std::memory_order_release);
    gHookStatus.tick = LocalPlayerEasyPlaceHook::hook() == 0;
    if (!gHookStatus.tick) {
        logger().error("Failed to install easy-place tick hook");
        return false;
    }
    gHookStatus.manualStart = GameModeStartBuildHook::hook() == 0;
    if (!gHookStatus.manualStart) {
        logger().error("Failed to install required manual-place start hook");
        return false;
    }
    gHookStatus.manualUseItem = GameModeUseItemHook::hook() == 0;
    if (!gHookStatus.manualUseItem) {
        logger().error("Failed to install required manual-place air-use hook");
        return false;
    }
    gHookStatus.manualStop = GameModeStopBuildHook::hook() == 0;
    if (!gHookStatus.manualStop) {
        logger().error("Failed to install required manual-place stop hook");
        return false;
    }
    gHookStatus.manualBuild = GameModeBuildBlockHook::hook() == 0;
    if (!gHookStatus.manualBuild) {
        logger().error("Failed to install required manual-place build hook");
        return false;
    }
    gPlacementHooksReady.store(true, std::memory_order_release);
    return true;
}

bool uninstallHook() {
    gPlacementHooksReady.store(false, std::memory_order_release);
    bool ok = true;
    auto remove = [&](bool& installed, auto unhook) {
        if (!installed) return;
        if (unhook()) installed = false;
        else ok = false;
    };
    remove(gHookStatus.manualBuild, [] { return GameModeBuildBlockHook::unhook(); });
    remove(gHookStatus.manualStop, [] { return GameModeStopBuildHook::unhook(); });
    remove(gHookStatus.manualUseItem, [] { return GameModeUseItemHook::unhook(); });
    remove(gHookStatus.manualStart, [] { return GameModeStartBuildHook::unhook(); });
    remove(gHookStatus.tick, [] { return LocalPlayerEasyPlaceHook::unhook(); });
    if (!ok) {
        logger().error("Failed to remove one or more placement hooks; retaining LHolo DLL");
    }
    return ok;
}

} // namespace lholo::place
