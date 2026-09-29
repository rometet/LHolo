// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "input/MenuInputGuard.h"

#include "app/HookLifecycle.h"
#include "overlay/CompanionBridge.h"
#include "structure/StructureLoader.h"

#include "ll/api/memory/Hook.h"

#include "mc/deps/input/Keyboard.h"
#include "mc/deps/input/MouseAction.h"
#include "mc/deps/input/MouseDevice.h"
#include "mc/deps/input/win/HIDControllerGameCoreDesktop.h"

#include <cstdint>

namespace lholo::input {
namespace {

MenuInputGuardStatus gInstallStatus{};
thread_local std::uint32_t gInputHandoffDepth{};

bool menuOwnsGameInput() {
    return gInputHandoffDepth == 0
        && (structure::isMenuInputCaptured()
            || lholo::overlay::companion::isVisible());
}

bool projectionOwnsMouseWheel(char actionButtonId) {
    return actionButtonId == MouseAction::ActionWheel && structure::scrollLockActive();
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
    if (!guard) {
        origin(actionButtonId, buttonData, x, y, dx, dy, forceMotionlessPointer);
        return;
    }
    try {
        if (menuOwnsGameInput() || projectionOwnsMouseWheel(actionButtonId)) return;
    } catch (...) {
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
    if (!guard) {
        origin(keyCode, originType);
        return;
    }
    try {
        if (menuOwnsGameInput() && keyCode != Keyboard::F11) return;
    } catch (...) {
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
    if (!guard) {
        origin(keyCode);
        return;
    }
    try {
        if (menuOwnsGameInput() && keyCode != Keyboard::F11) return;
    } catch (...) {
    }
    origin(keyCode);
}

} // namespace

MenuInputHandoffScope::MenuInputHandoffScope() { ++gInputHandoffDepth; }

MenuInputHandoffScope::~MenuInputHandoffScope() {
    if (gInputHandoffDepth != 0) --gInputHandoffDepth;
}

MenuInputGuardStatus installMenuInputGuard() {
    if (!gInstallStatus.mouseInputHookInstalled) {
        gInstallStatus.mouseInputHookInstalled = MenuMouseInputHook::hook() == 0;
    }
    if (!gInstallStatus.keyDownInputHookInstalled) {
        gInstallStatus.keyDownInputHookInstalled = MenuKeyDownInputHook::hook() == 0;
    }
    if (!gInstallStatus.keyUpInputHookInstalled) {
        gInstallStatus.keyUpInputHookInstalled = MenuKeyUpInputHook::hook() == 0;
    }
    return gInstallStatus;
}

bool uninstallMenuInputGuard() {
    bool ok = true;
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
