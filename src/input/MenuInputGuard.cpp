// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "input/MenuInputGuard.h"

#include "app/HookLifecycle.h"
#include "app/NativeCallbackBoundary.h"
#include "overlay/CompanionBridge.h"
#include "structure/StructureLoader.h"
#include "structure/StructureUiState.h"

#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/gui/controls/TextEditComponent.h"

#include "mc/deps/input/Keyboard.h"
#include "mc/deps/input/MouseAction.h"
#include "mc/deps/input/MouseDevice.h"
#include "mc/deps/input/win/HIDControllerGameCoreDesktop.h"

#include <cstdint>
#include <Windows.h>
#include <atomic>
#include <array>
#include <algorithm>

namespace lholo::input {
namespace {

MenuInputGuardStatus gInstallStatus{};
std::atomic_bool gInputGuardReady{};
std::array<bool,1> gTextInputHooksInstalled{};
std::atomic_bool gScreenFocusInstalled{};
std::atomic_bool gWindowMessageSourceReady{};
thread_local std::uint32_t gInputHandoffDepth{};

bool menuOwnsGameInput() {
    return gInputHandoffDepth == 0
        && (structure::isMenuInputCaptured()
            || lholo::overlay::companion::isVisible());
}

bool projectionOwnsMouseWheel(char actionButtonId) {
    return actionButtonId == MouseAction::ActionWheel && structure::scrollLockActive();
}

void trackTextInput(NativeTextInputFlag flag, bool blocked) {
    structure::detail::StructureUiState::getInstance().setNativeTextInputFlag(flag,blocked);
}

// Native text/IME transitions arrive synchronously before a following key.
// These callbacks publish values only and always forward the game callback.
LL_TYPE_INSTANCE_HOOK(MenuScreenTextFocusHook,ll::memory::HookPriority::Highest,
    ScreenView,&ScreenView::_fireSelectedStateChangeEvent,void,
    TextEditComponent const& component,bool selected) {
    app::hook_lifecycle::DetourGuard guard;
    bool const tracking=guard && gInputGuardReady.load(std::memory_order_acquire);
    auto& ui=structure::detail::StructureUiState::getInstance();
    auto const token=tracking && !selected
        ? ui.nativeTextInputToken(NativeTextInputFlag::Focus):0;
    if(tracking && selected) trackTextInput(NativeTextInputFlag::Focus,true);
    origin(component,selected);
    if(tracking && !selected && gInputGuardReady.load(std::memory_order_acquire))
        ui.clearNativeTextInputFlagIfCurrent(NativeTextInputFlag::Focus,token);
}
// 26.60 no longer exports HIDControllerGameCoreDesktop's keyboard/IME hooks.
// Keep the native ScreenView focus event above and consume actual Win32
// IME/window activation transitions from LHolo's existing WndProc, avoiding
// guessed native offsets, background hooks or swallowed game text messages.
LL_TYPE_INSTANCE_HOOK(
    MenuMouseInputHook,
    ll::memory::HookPriority::Highest,
    MouseDevice,
    &MouseDevice::feed,
    void,
    char  actionButtonId,
    schar buttonData,
    short x,
    short y,
    short dx,
    short dy,
    bool  forceMotionlessPointer
) {
    app::hook_lifecycle::DetourGuard guard;
    if (!guard || !gInputGuardReady.load(std::memory_order_acquire)) {
        origin(actionButtonId, buttonData, x, y, dx, dy, forceMotionlessPointer);
        return;
    }
    try {
        if (menuOwnsGameInput() || projectionOwnsMouseWheel(actionButtonId)) return;
    } catch (std::exception const& exception) {
        app::reportNativeCallbackFailure("menu input guard", exception.what());
    } catch (...) {
        app::reportNativeCallbackFailure("menu input guard", "unknown C++ exception");
    }
    origin(actionButtonId, buttonData, x, y, dx, dy, forceMotionlessPointer);
}

LL_TYPE_INSTANCE_HOOK(
    MenuKeyDownInputHook,
    ll::memory::HookPriority::Highest,
    HIDControllerGameCoreDesktop,
    &HIDControllerGameCoreDesktop::$onKeyDown,
    void,
    int                                                 keyCode,
    Bedrock::Input::KeyboardEventProcessor::InputOrigin originType
) {
    app::hook_lifecycle::DetourGuard guard;
    if (!guard || !gInputGuardReady.load(std::memory_order_acquire)) {
        origin(keyCode, originType);
        return;
    }
    try {
        if (menuOwnsGameInput() && keyCode != Keyboard::F11) return;
    } catch (std::exception const& exception) {
        app::reportNativeCallbackFailure("menu input guard", exception.what());
    } catch (...) {
        app::reportNativeCallbackFailure("menu input guard", "unknown C++ exception");
    }
    origin(keyCode, originType);
}

LL_TYPE_INSTANCE_HOOK(
    MenuKeyUpInputHook,
    ll::memory::HookPriority::Highest,
    HIDControllerGameCoreDesktop,
    &HIDControllerGameCoreDesktop::$onKeyUp,
    void,
    int keyCode
) {
    app::hook_lifecycle::DetourGuard guard;
    if (!guard || !gInputGuardReady.load(std::memory_order_acquire)) {
        origin(keyCode);
        return;
    }
    try {
        if (menuOwnsGameInput() && keyCode != Keyboard::F11) return;
    } catch (std::exception const& exception) {
        app::reportNativeCallbackFailure("menu input guard", exception.what());
    } catch (...) {
        app::reportNativeCallbackFailure("menu input guard", "unknown C++ exception");
    }
    origin(keyCode);
}

} // namespace

void observeNativeWindowMessage(unsigned int message, std::uintptr_t wParam) noexcept {
    if (!gInputGuardReady.load(std::memory_order_acquire)) return;
    auto& ui=structure::detail::StructureUiState::getInstance();
    if (!gWindowMessageSourceReady.exchange(true,std::memory_order_acq_rel)
        && gTextInputHooksInstalled[0])
        ui.setNativeTextInputHooksReady(true);
    switch (message) {
    case WM_IME_STARTCOMPOSITION:
        trackTextInput(NativeTextInputFlag::Ime,true);
        break;
    case WM_IME_ENDCOMPOSITION:
        ui.clearNativeTextInputFlagIfCurrent(
            NativeTextInputFlag::Ime,ui.nativeTextInputToken(NativeTextInputFlag::Ime));
        break;
    case WM_KILLFOCUS:
        trackTextInput(NativeTextInputFlag::GameplayDisabled,true);
        break;
    case WM_SETFOCUS:
        ui.clearNativeTextInputFlagIfCurrent(
            NativeTextInputFlag::GameplayDisabled,
            ui.nativeTextInputToken(NativeTextInputFlag::GameplayDisabled));
        break;
    case WM_ACTIVATEAPP:
        if (!wParam) trackTextInput(NativeTextInputFlag::GameplayDisabled,true);
        else ui.clearNativeTextInputFlagIfCurrent(
            NativeTextInputFlag::GameplayDisabled,
            ui.nativeTextInputToken(NativeTextInputFlag::GameplayDisabled));
        break;
    default: break;
    }
}
MenuInputHandoffScope::MenuInputHandoffScope() { ++gInputHandoffDepth; }

MenuInputHandoffScope::~MenuInputHandoffScope() {
    if (gInputHandoffDepth != 0) --gInputHandoffDepth;
}

MenuInputGuardStatus installMenuInputGuard() {
    structure::detail::StructureUiState::getInstance().setNativeTextInputHooksReady(false);
    if (!gInstallStatus.mouseInputHookInstalled) {
        gInstallStatus.mouseInputHookInstalled = MenuMouseInputHook::hook() == 0;
    }
    if (!gInstallStatus.keyDownInputHookInstalled) {
        gInstallStatus.keyDownInputHookInstalled = MenuKeyDownInputHook::hook() == 0;
    }
    if (!gInstallStatus.keyUpInputHookInstalled) {
        gInstallStatus.keyUpInputHookInstalled = MenuKeyUpInputHook::hook() == 0;
    }
    if(!gTextInputHooksInstalled[0])gTextInputHooksInstalled[0]=MenuScreenTextFocusHook::hook()==0;
    gScreenFocusInstalled.store(gTextInputHooksInstalled[0],std::memory_order_release);
    gInstallStatus.textInputHooksInstalled=gTextInputHooksInstalled[0]
        && gWindowMessageSourceReady.load(std::memory_order_acquire);
    structure::detail::StructureUiState::getInstance().setNativeTextInputHooksReady(
        gInstallStatus.textInputHooksInstalled);    gInputGuardReady.store(
        gInstallStatus.mouseInputHookInstalled && gInstallStatus.keyDownInputHookInstalled
            && gInstallStatus.keyUpInputHookInstalled,
        std::memory_order_release
    );
    return gInstallStatus;
}

bool uninstallMenuInputGuard() {
    structure::detail::StructureUiState::getInstance().setNativeTextInputHooksReady(false);
    gInputGuardReady.store(false, std::memory_order_release);
    gScreenFocusInstalled.store(false,std::memory_order_release);
    bool ok = true;
    auto removeTextHook=[&](std::size_t index,bool removed){
        if(removed)gTextInputHooksInstalled[index]=false;
        else ok=false;
    };
    if(gTextInputHooksInstalled[0])removeTextHook(0,MenuScreenTextFocusHook::unhook());
    gWindowMessageSourceReady.store(false,std::memory_order_release);
    gInstallStatus.textInputHooksInstalled=false;
    if (gInstallStatus.keyUpInputHookInstalled) {
        if (MenuKeyUpInputHook::unhook()) gInstallStatus.keyUpInputHookInstalled = false;
        else ok = false;
    }
    if (gInstallStatus.keyDownInputHookInstalled) {
        if (MenuKeyDownInputHook::unhook()) gInstallStatus.keyDownInputHookInstalled = false;
        else ok = false;
    }
    if (gInstallStatus.mouseInputHookInstalled) {
        if (MenuMouseInputHook::unhook()) gInstallStatus.mouseInputHookInstalled = false;
        else ok = false;
    }
    return ok;
}

} // namespace lholo::input
