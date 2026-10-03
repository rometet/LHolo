// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "input/MenuInputGuard.h"

#include "app/HookLifecycle.h"
#include "app/NativeCallbackBoundary.h"
#include "overlay/CompanionBridge.h"
#include "structure/StructureLoader.h"
#include "structure/StructureUiState.h"

#include "ll/api/memory/Hook.h"

#include "mc/deps/input/Keyboard.h"
#include "mc/deps/input/MouseAction.h"
#include "mc/deps/input/MouseDevice.h"
#include "mc/deps/input/win/HIDControllerGameCoreDesktop.h"

#include <cstdint>
#include <atomic>
#include <array>
#include <algorithm>

namespace lholo::input {
namespace {

MenuInputGuardStatus gInstallStatus{};
std::atomic_bool gInputGuardReady{};
std::array<bool,6> gTextInputHooksInstalled{};
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
LL_TYPE_INSTANCE_HOOK(MenuTextFocusGainedHook,ll::memory::HookPriority::Highest,
    HIDControllerGameCoreDesktop,&HIDControllerGameCoreDesktop::$onTextEditComponentGainedFocus,
    void,::std::string_view const currentText,int maxLength) {
    app::hook_lifecycle::DetourGuard guard;
    if(guard && gInputGuardReady.load(std::memory_order_acquire))trackTextInput(NativeTextInputFlag::Focus,true);
    origin(currentText,maxLength);
}
LL_TYPE_INSTANCE_HOOK(MenuTextFocusLostHook,ll::memory::HookPriority::Highest,
    HIDControllerGameCoreDesktop,&HIDControllerGameCoreDesktop::$onTextEditComponentLostFocus,void) {
    app::hook_lifecycle::DetourGuard guard;
    bool const tracking=guard && gInputGuardReady.load(std::memory_order_acquire);
    auto const token=tracking?structure::detail::StructureUiState::getInstance().nativeTextInputToken(NativeTextInputFlag::Focus):0;
    origin();
    if(tracking && gInputGuardReady.load(std::memory_order_acquire))structure::detail::StructureUiState::getInstance().clearNativeTextInputFlagIfCurrent(NativeTextInputFlag::Focus,token);
}
LL_TYPE_INSTANCE_HOOK(MenuKeyboardShowHook,ll::memory::HookPriority::Highest,
    HIDControllerGameCoreDesktop,&HIDControllerGameCoreDesktop::$onShowKeyboard,
    void,::std::string_view const currentText,int maxLength,bool isMultiline,::InputMode inputMode) {
    app::hook_lifecycle::DetourGuard guard;
    if(guard && gInputGuardReady.load(std::memory_order_acquire))trackTextInput(NativeTextInputFlag::Keyboard,true);
    origin(currentText,maxLength,isMultiline,inputMode);
}
LL_TYPE_INSTANCE_HOOK(MenuKeyboardHideHook,ll::memory::HookPriority::Highest,
    HIDControllerGameCoreDesktop,&HIDControllerGameCoreDesktop::$onHideKeyboard,void) {
    app::hook_lifecycle::DetourGuard guard;
    bool const tracking=guard && gInputGuardReady.load(std::memory_order_acquire);
    auto const token=tracking?structure::detail::StructureUiState::getInstance().nativeTextInputToken(NativeTextInputFlag::Keyboard):0;
    origin();
    if(tracking && gInputGuardReady.load(std::memory_order_acquire))structure::detail::StructureUiState::getInstance().clearNativeTextInputFlagIfCurrent(NativeTextInputFlag::Keyboard,token);
}
LL_TYPE_INSTANCE_HOOK(MenuImeStartHook,ll::memory::HookPriority::Highest,
    HIDControllerGameCoreDesktop,&HIDControllerGameCoreDesktop::$imeStartComposition,void) {
    app::hook_lifecycle::DetourGuard guard;
    if(guard && gInputGuardReady.load(std::memory_order_acquire))trackTextInput(NativeTextInputFlag::Ime,true);
    origin();
}
LL_TYPE_INSTANCE_HOOK(MenuImeEndHook,ll::memory::HookPriority::Highest,
    HIDControllerGameCoreDesktop,&HIDControllerGameCoreDesktop::$imeEndComposition,void) {
    app::hook_lifecycle::DetourGuard guard;
    bool const tracking=guard && gInputGuardReady.load(std::memory_order_acquire);
    auto const token=tracking?structure::detail::StructureUiState::getInstance().nativeTextInputToken(NativeTextInputFlag::Ime):0;
    origin();
    if(tracking && gInputGuardReady.load(std::memory_order_acquire))structure::detail::StructureUiState::getInstance().clearNativeTextInputFlagIfCurrent(NativeTextInputFlag::Ime,token);
}

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
    if(!gTextInputHooksInstalled[0])gTextInputHooksInstalled[0]=MenuTextFocusGainedHook::hook()==0;
    if(!gTextInputHooksInstalled[1])gTextInputHooksInstalled[1]=MenuTextFocusLostHook::hook()==0;
    if(!gTextInputHooksInstalled[2])gTextInputHooksInstalled[2]=MenuKeyboardShowHook::hook()==0;
    if(!gTextInputHooksInstalled[3])gTextInputHooksInstalled[3]=MenuKeyboardHideHook::hook()==0;
    if(!gTextInputHooksInstalled[4])gTextInputHooksInstalled[4]=MenuImeStartHook::hook()==0;
    if(!gTextInputHooksInstalled[5])gTextInputHooksInstalled[5]=MenuImeEndHook::hook()==0;
    gInstallStatus.textInputHooksInstalled=std::all_of(gTextInputHooksInstalled.begin(),gTextInputHooksInstalled.end(),[](bool installed){return installed;});
    structure::detail::StructureUiState::getInstance().setNativeTextInputHooksReady(gInstallStatus.textInputHooksInstalled);
    gInputGuardReady.store(
        gInstallStatus.mouseInputHookInstalled && gInstallStatus.keyDownInputHookInstalled
            && gInstallStatus.keyUpInputHookInstalled,
        std::memory_order_release
    );
    return gInstallStatus;
}

bool uninstallMenuInputGuard() {
    structure::detail::StructureUiState::getInstance().setNativeTextInputHooksReady(false);
    gInputGuardReady.store(false, std::memory_order_release);
    bool ok = true;
    auto removeTextHook=[&](std::size_t index,bool removed){
        if(removed)gTextInputHooksInstalled[index]=false;
        else ok=false;
    };
    if(gTextInputHooksInstalled[5])removeTextHook(5,MenuImeEndHook::unhook());
    if(gTextInputHooksInstalled[4])removeTextHook(4,MenuImeStartHook::unhook());
    if(gTextInputHooksInstalled[3])removeTextHook(3,MenuKeyboardHideHook::unhook());
    if(gTextInputHooksInstalled[2])removeTextHook(2,MenuKeyboardShowHook::unhook());
    if(gTextInputHooksInstalled[1])removeTextHook(1,MenuTextFocusLostHook::unhook());
    if(gTextInputHooksInstalled[0])removeTextHook(0,MenuTextFocusGainedHook::unhook());
    gInstallStatus.textInputHooksInstalled=std::all_of(gTextInputHooksInstalled.begin(),gTextInputHooksInstalled.end(),[](bool installed){return installed;});
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
