#include "overlay/CompanionBridge.h"
#include "app/NativeCallbackBoundary.h"
#include "overlay/CompanionCallbackStore.h"
#include "overlay/SharedFontPreparation.h"

#include "plugin/LHolo.h"
#include "structure/StructureLoader.h"
#include "ll/api/mod/NativeMod.h"

#include <Windows.h>
#include <imgui.h>


namespace lholo::overlay::companion {
namespace {

using detail::Registration;
detail::CallbackStore gCallbacks;
detail::SharedFontPreparation gSharedFonts;
thread_local detail::SharedFrameLease gSharedFrame;

auto& logger() {
    return LHolo::getInstance().getSelf().getLogger();
}

auto acquireCallbacks() noexcept { return gCallbacks.acquire(); }

bool setVisible(bool value) noexcept {
    auto lease = gCallbacks.changeVisibility(value);
    if (!lease.active()) return false;
    if (lease.changed && lease.registration.stateChanged) {
        lease.registration.stateChanged(value);
    }
    return true;
}

bool hotkeyMatches(unsigned int key) noexcept {
    auto lease = acquireCallbacks();
    return lease.active() && lease.registration.hotkey == key;
}
bool registerProviderV2(
    void* owner,
    unsigned hotkey,
    DrawFn drawGui,
    DrawFn drawHud,
    HudNeededFn hudNeeded,
    StateFn stateChanged,
    unsigned imguiVersion,
    std::size_t ioSize,
    std::size_t styleSize,
    std::size_t drawVertSize
) noexcept {
    if (!owner || !drawGui || !stateChanged || hotkey == 0) return false;
    if (imguiVersion != IMGUI_VERSION_NUM
        || ioSize != sizeof(ImGuiIO)
        || styleSize != sizeof(ImGuiStyle)
        || drawVertSize != sizeof(ImDrawVert)) {
        app::invokeNativeCallback([&] { logger().error(
            "Companion GUI bridge rejected: ImGui ABI mismatch "
            "(version {} vs {}, io {} vs {}, style {} vs {}, vert {} vs {})",
            imguiVersion, IMGUI_VERSION_NUM,
            ioSize, sizeof(ImGuiIO),
            styleSize, sizeof(ImGuiStyle),
            drawVertSize, sizeof(ImDrawVert)
        ); }, [](char const* reason) noexcept {
            app::reportNativeCallbackFailure("companion ABI diagnostic", reason);
        });
        return false;
    }

    Registration next{};
    next.owner = owner;
    next.hotkey = hotkey;
    next.drawGui = drawGui;
    next.drawHud = drawHud;
    next.hudNeeded = hudNeeded;
    next.stateChanged = stateChanged;
    if (!gCallbacks.publish(next)) return false;
    app::invokeNativeCallback([] { logger().info("Companion GUI bridge v2 registered"); },
        [](char const* reason) noexcept { app::reportNativeCallbackFailure("companion registration diagnostic", reason); });
    return true;
}

bool registerProviderV3(
    void* owner,
    unsigned hotkey,
    RenderV3Fn render,
    WindowMessageV3Fn windowMessage,
    HudNeededFn hudNeeded,
    StateFn stateChanged,
    GraphicsResetV3Fn resetGraphics
) noexcept {
    if (!owner || !render || !windowMessage || !stateChanged || !resetGraphics || hotkey == 0)
        return false;

    Registration next{};
    next.owner = owner;
    next.hotkey = hotkey;
    next.hudNeeded = hudNeeded;
    next.stateChanged = stateChanged;
    next.renderV3 = render;
    next.windowMessageV3 = windowMessage;
    next.resetGraphicsV3 = resetGraphics;
    next.independentRenderer = true;
    if (!gCallbacks.publish(next)) return false;
    app::invokeNativeCallback([] { logger().info("Companion GUI bridge v3 registered (independent ImGui state)"); },
        [](char const* reason) noexcept { app::reportNativeCallbackFailure("companion registration diagnostic", reason); });
    return true;
}

bool retireProvider(void* owner, bool shutdown) noexcept {
    auto retirement = gCallbacks.beginRetirement(owner, shutdown);
    if (!retirement) return false;
    gCallbacks.waitForReaders();
    auto const& registration = retirement->registration;
    if (retirement->wasVisible && registration.stateChanged) registration.stateChanged(false);
    if (registration.resetGraphicsV3) registration.resetGraphicsV3();
    if (registration.resetSharedGraphics) registration.resetSharedGraphics();
    gCallbacks.finishRetirement();
    return true;
}

bool unregisterProvider(void* owner) noexcept {
    if (!retireProvider(owner, false)) return false;
    app::invokeNativeCallback([] { logger().info("Companion GUI bridge unregistered"); },
        [](char const* reason) noexcept { app::reportNativeCallbackFailure("companion retirement diagnostic", reason); });
    return true;
}

bool registeredFor(void* owner) noexcept {
    return owner && gCallbacks.registered(owner);
}
bool setFontInitializer(void* owner, FontInitializerFn initializer) noexcept {
    return gCallbacks.setFontInitializer(owner, initializer);
}
bool setSharedGraphics(void* owner, SharedGraphicsV2Fn beforeFrame, GraphicsResetV3Fn reset) noexcept {
    return gCallbacks.setSharedGraphics(owner, beforeFrame, reset);
}
} // namespace

bool isRegistered() noexcept {
    return gCallbacks.registered();
}

bool isVisible() noexcept {
    return gCallbacks.visible();
}

bool hudNeeded() noexcept {
    auto lease = acquireCallbacks();
    return lease.active() && lease.registration.hudNeeded
        && lease.registration.hudNeeded();
}

bool inputCaptured() noexcept {
    return isVisible() || structure::isMenuInputCaptured();
}

bool usesIndependentRenderer() noexcept {
    auto lease = acquireCallbacks();
    return lease.active() && lease.registration.independentRenderer;
}

bool handleHotkeyKeyDown(unsigned int key, bool repeated) noexcept {
    if (!hotkeyMatches(key)) return false;
    if (!repeated) (void)setVisible(!isVisible());
    return true;
}

bool handleHotkeyKeyUp(unsigned int key) noexcept {
    return hotkeyMatches(key);
}

void close() noexcept {
    (void)setVisible(false);
}

void drawGui(void* imguiContext) noexcept {
    auto lease = acquireCallbacks();
    if (!lease.active() || lease.registration.independentRenderer
        || !isVisible() || !lease.registration.drawGui) return;
    lease.registration.drawGui(imguiContext);
}

void drawHud(void* imguiContext) noexcept {
    auto lease = acquireCallbacks();
    if (!lease.active() || lease.registration.independentRenderer
        || isVisible() || !lease.registration.drawHud) return;
    if (lease.registration.hudNeeded && !lease.registration.hudNeeded()) return;
    lease.registration.drawHud(imguiContext);
}

bool beginSharedFrame(void* device, void* deviceContext) noexcept {
    if (!gSharedFrame.begin(gCallbacks)) return false;
    auto const registration = gSharedFrame.registration();
    if (registration->beforeSharedFrame) registration->beforeSharedFrame(device, deviceContext);
    return true;
}
void endSharedFrame() noexcept { gSharedFrame.end(); }

bool prepareSharedFonts(void* imguiContext) noexcept {
    auto lease = acquireCallbacks();
    auto const& registration = lease.registration;
    if (!lease.active() || registration.independentRenderer || !registration.fontInitializer
        || !gSharedFonts.needsPreparation(imguiContext, registration.owner, registration.fontGeneration)) return false;
    auto& io = ImGui::GetIO();
    auto const defaultFont = io.FontDefault;
    auto const globalScale = io.FontGlobalScale;
    auto const style = ImGui::GetStyle();
    // The lease prevents provider retirement/unload while resource initialization runs.
    registration.fontInitializer(imguiContext);
    io.FontDefault = defaultFont;
    io.FontGlobalScale = globalScale;
    ImGui::GetStyle() = style;
    gSharedFonts.prepared(imguiContext, registration.owner, registration.fontGeneration);
    return true;
}
void resetSharedFonts() noexcept { gSharedFonts.reset(); }

void renderIndependent(void* device, void* deviceContext, void* window, bool guiVisible) noexcept {
    auto lease = acquireCallbacks();
    if (!lease.active() || !lease.registration.independentRenderer
        || !lease.registration.renderV3) return;
    if (guiVisible != isVisible()) return;
    if (!guiVisible && lease.registration.hudNeeded && !lease.registration.hudNeeded()) return;
    lease.registration.renderV3(device, deviceContext, window, guiVisible);
}

void forwardWindowMessage(
    void* window,
    unsigned message,
    std::uintptr_t wParam,
    std::intptr_t lParam
) noexcept {
    auto lease = acquireCallbacks();
    if (!lease.active() || !lease.registration.independentRenderer
        || !lease.registration.windowMessageV3 || !isVisible()) return;
    lease.registration.windowMessageV3(window, message, wParam, lParam);
}

void resetGraphics() noexcept {
    auto lease = acquireCallbacks();
    if (lease.active() && !lease.registration.independentRenderer) {
        if (lease.registration.resetSharedGraphics) lease.registration.resetSharedGraphics();
        return;
    }
    if (!lease.active() || !lease.registration.independentRenderer
        || !lease.registration.resetGraphicsV3) return;
    lease.registration.resetGraphicsV3();
}

bool beginSession() noexcept { return gCallbacks.beginSession(); }
bool shutdown() noexcept { return retireProvider(nullptr, true); }
} // namespace lholo::overlay::companion

extern "C" __declspec(dllexport) bool __cdecl lholo_register_companion_gui_v2(
    void* owner,
    unsigned hotkey,
    lholo::overlay::companion::DrawFn drawGui,
    lholo::overlay::companion::DrawFn drawHud,
    lholo::overlay::companion::HudNeededFn hudNeeded,
    lholo::overlay::companion::StateFn stateChanged,
    unsigned imguiVersion,
    std::size_t ioSize,
    std::size_t styleSize,
    std::size_t drawVertSize
) noexcept {
    return lholo::overlay::companion::registerProviderV2(
        owner, hotkey, drawGui, drawHud, hudNeeded, stateChanged,
        imguiVersion, ioSize, styleSize, drawVertSize
    );
}

extern "C" __declspec(dllexport) bool __cdecl lholo_register_companion_gui_v3(
    void* owner,
    unsigned hotkey,
    lholo::overlay::companion::RenderV3Fn render,
    lholo::overlay::companion::WindowMessageV3Fn windowMessage,
    lholo::overlay::companion::HudNeededFn hudNeeded,
    lholo::overlay::companion::StateFn stateChanged,
    lholo::overlay::companion::GraphicsResetV3Fn resetGraphics
) noexcept {
    return lholo::overlay::companion::registerProviderV3(
        owner, hotkey, render, windowMessage, hudNeeded, stateChanged, resetGraphics
    );
}

extern "C" __declspec(dllexport) bool __cdecl lholo_set_companion_gui_font_initializer_v2(
    void* owner,
    lholo::overlay::companion::FontInitializerFn initializer
) noexcept {
    return lholo::overlay::companion::setFontInitializer(owner, initializer);
}
extern "C" __declspec(dllexport) bool __cdecl lholo_set_companion_gui_graphics_callbacks_v2(
    void* owner,
    lholo::overlay::companion::SharedGraphicsV2Fn beforeFrame,
    lholo::overlay::companion::GraphicsResetV3Fn reset
) noexcept {
    return lholo::overlay::companion::setSharedGraphics(owner, beforeFrame, reset);
}

extern "C" __declspec(dllexport) bool __cdecl lholo_unregister_companion_gui_v2(void* owner) noexcept {
    return lholo::overlay::companion::unregisterProvider(owner);
}
extern "C" __declspec(dllexport) bool __cdecl lholo_unregister_companion_gui_v3(void* owner) noexcept {
    return lholo::overlay::companion::unregisterProvider(owner);
}
extern "C" __declspec(dllexport) bool __cdecl lholo_companion_gui_registered_v2(void* owner) noexcept {
    return lholo::overlay::companion::registeredFor(owner);
}
extern "C" __declspec(dllexport) bool __cdecl lholo_companion_gui_registered_v3(void* owner) noexcept {
    return lholo::overlay::companion::registeredFor(owner);
}
extern "C" __declspec(dllexport) void __cdecl lholo_close_companion_gui_v2() noexcept {
    lholo::overlay::companion::close();
}
extern "C" __declspec(dllexport) void __cdecl lholo_close_companion_gui_v3() noexcept {
    lholo::overlay::companion::close();
}
extern "C" __declspec(dllexport) bool __cdecl lholo_menu_input_captured_v2() noexcept {
    return lholo::overlay::companion::inputCaptured();
}
extern "C" __declspec(dllexport) bool __cdecl lholo_menu_input_captured_v3() noexcept {
    return lholo::overlay::companion::inputCaptured();
}
