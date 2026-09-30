#include "overlay/CompanionBridge.h"

#include "plugin/LHolo.h"
#include "structure/StructureLoader.h"
#include "ll/api/mod/NativeMod.h"

#include <Windows.h>
#include <imgui.h>

#include <atomic>
#include <mutex>
#include <thread>

namespace lholo::overlay::companion {
namespace {

struct Registration {
    void*              owner{};
    unsigned           hotkey{};
    DrawFn             drawGui{};
    DrawFn             drawHud{};
    HudNeededFn        hudNeeded{};
    StateFn            stateChanged{};
    RenderV3Fn         renderV3{};
    WindowMessageV3Fn  windowMessageV3{};
    GraphicsResetV3Fn  resetGraphicsV3{};
    bool                independentRenderer{};
};

std::mutex       gMutex;
Registration     gRegistration;
std::atomic_bool gRegistered{};
std::atomic_bool gVisible{};
std::atomic_uint gReaders{};
thread_local unsigned int gReaderDepth{};

auto& logger() {
    return LHolo::getInstance().getSelf().getLogger();
}

struct CallbackLease {
    Registration registration{};
    bool active{};

    CallbackLease() = default;
    CallbackLease(Registration value, bool ownsReader)
        : registration(value), active(ownsReader) {
        if (active) ++gReaderDepth;
    }
    CallbackLease(CallbackLease const&) = delete;
    CallbackLease& operator=(CallbackLease const&) = delete;
    CallbackLease(CallbackLease&& other) noexcept
        : registration(other.registration), active(other.active) {
        other.active = false;
    }
    CallbackLease& operator=(CallbackLease&&) = delete;

    ~CallbackLease() {
        if (active) {
            --gReaderDepth;
            gReaders.fetch_sub(1, std::memory_order_release);
        }
    }
};

CallbackLease acquireCallbacks() noexcept {
    std::lock_guard lock(gMutex);
    if (!gRegistered.load(std::memory_order_acquire)) return {};
    gReaders.fetch_add(1, std::memory_order_acq_rel);
    return CallbackLease{gRegistration, true};
}

void waitForReaders() noexcept {
    while (gReaders.load(std::memory_order_acquire) != 0) {
        std::this_thread::yield();
    }
}

bool setVisible(bool value) noexcept {
    StateFn callback{};
    {
        std::lock_guard lock(gMutex);
        if (!gRegistered.load(std::memory_order_acquire)) return false;
        if (gVisible.load(std::memory_order_relaxed) == value) return true;
        gVisible.store(value, std::memory_order_release);
        callback = gRegistration.stateChanged;
        if (callback) gReaders.fetch_add(1, std::memory_order_acq_rel);
    }
    if (callback) {
        ++gReaderDepth;
        callback(value);
        --gReaderDepth;
        gReaders.fetch_sub(1, std::memory_order_release);
    }
    return true;
}

bool hotkeyMatches(unsigned int key) noexcept {
    std::lock_guard lock(gMutex);
    return gRegistered.load(std::memory_order_acquire)
        && gRegistration.hotkey == key;
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
        logger().error(
            "Companion GUI bridge rejected: ImGui ABI mismatch "
            "(version {} vs {}, io {} vs {}, style {} vs {}, vert {} vs {})",
            imguiVersion, IMGUI_VERSION_NUM,
            ioSize, sizeof(ImGuiIO),
            styleSize, sizeof(ImGuiStyle),
            drawVertSize, sizeof(ImDrawVert)
        );
        return false;
    }

    std::lock_guard lock(gMutex);
    if (gRegistered.load(std::memory_order_acquire)
        && gRegistration.owner != owner) {
        return false;
    }
    Registration next{};
    next.owner = owner;
    next.hotkey = hotkey;
    next.drawGui = drawGui;
    next.drawHud = drawHud;
    next.hudNeeded = hudNeeded;
    next.stateChanged = stateChanged;
    gRegistration = next;
    gVisible.store(false, std::memory_order_release);
    gRegistered.store(true, std::memory_order_release);
    logger().info("Companion GUI bridge v2 registered");
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

    std::lock_guard lock(gMutex);
    if (gRegistered.load(std::memory_order_acquire)
        && gRegistration.owner != owner) {
        return false;
    }
    Registration next{};
    next.owner = owner;
    next.hotkey = hotkey;
    next.hudNeeded = hudNeeded;
    next.stateChanged = stateChanged;
    next.renderV3 = render;
    next.windowMessageV3 = windowMessage;
    next.resetGraphicsV3 = resetGraphics;
    next.independentRenderer = true;
    gRegistration = next;
    gVisible.store(false, std::memory_order_release);
    gRegistered.store(true, std::memory_order_release);
    logger().info("Companion GUI bridge v3 registered (independent ImGui state)");
    return true;
}

bool unregisterProvider(void* owner) noexcept {
    if (gReaderDepth != 0) return false;

    StateFn stateChanged{};
    GraphicsResetV3Fn resetGraphics{};
    bool wasVisible{};
    {
        std::lock_guard lock(gMutex);
        if (!gRegistered.load(std::memory_order_acquire)) return true;
        if (!owner || gRegistration.owner != owner) return false;
        gRegistered.store(false, std::memory_order_release);
        wasVisible = gVisible.exchange(false, std::memory_order_acq_rel);
        stateChanged = gRegistration.stateChanged;
        resetGraphics = gRegistration.resetGraphicsV3;
    }

    waitForReaders();
    if (wasVisible && stateChanged) stateChanged(false);
    if (resetGraphics) resetGraphics();

    {
        std::lock_guard lock(gMutex);
        if (gRegistration.owner == owner) gRegistration = {};
    }
    logger().info("Companion GUI bridge unregistered");
    return true;
}

bool registeredFor(void* owner) noexcept {
    std::lock_guard lock(gMutex);
    return gRegistered.load(std::memory_order_acquire)
        && gRegistration.owner == owner;
}

} // namespace

bool isRegistered() noexcept {
    return gRegistered.load(std::memory_order_acquire);
}

bool isVisible() noexcept {
    return gRegistered.load(std::memory_order_acquire)
        && gVisible.load(std::memory_order_acquire);
}

bool hudNeeded() noexcept {
    auto lease = acquireCallbacks();
    return lease.active && lease.registration.hudNeeded
        && lease.registration.hudNeeded();
}

bool inputCaptured() noexcept {
    return isVisible() || structure::isMenuInputCaptured();
}

bool usesIndependentRenderer() noexcept {
    std::lock_guard lock(gMutex);
    return gRegistered.load(std::memory_order_acquire)
        && gRegistration.independentRenderer;
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
    if (!lease.active || lease.registration.independentRenderer
        || !isVisible() || !lease.registration.drawGui) return;
    lease.registration.drawGui(imguiContext);
}

void drawHud(void* imguiContext) noexcept {
    auto lease = acquireCallbacks();
    if (!lease.active || lease.registration.independentRenderer
        || isVisible() || !lease.registration.drawHud) return;
    if (lease.registration.hudNeeded && !lease.registration.hudNeeded()) return;
    lease.registration.drawHud(imguiContext);
}

void renderIndependent(void* device, void* deviceContext, void* window, bool guiVisible) noexcept {
    auto lease = acquireCallbacks();
    if (!lease.active || !lease.registration.independentRenderer
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
    if (!lease.active || !lease.registration.independentRenderer
        || !lease.registration.windowMessageV3 || !isVisible()) return;
    lease.registration.windowMessageV3(window, message, wParam, lParam);
}

void resetGraphics() noexcept {
    auto lease = acquireCallbacks();
    if (!lease.active || !lease.registration.independentRenderer
        || !lease.registration.resetGraphicsV3) return;
    lease.registration.resetGraphicsV3();
}

void shutdown() noexcept {
    StateFn stateChanged{};
    GraphicsResetV3Fn resetGraphics{};
    bool wasVisible{};
    {
        std::lock_guard lock(gMutex);
        if (!gRegistered.load(std::memory_order_acquire)) return;
        gRegistered.store(false, std::memory_order_release);
        wasVisible = gVisible.exchange(false, std::memory_order_acq_rel);
        stateChanged = gRegistration.stateChanged;
        resetGraphics = gRegistration.resetGraphicsV3;
    }
    waitForReaders();
    if (wasVisible && stateChanged) stateChanged(false);
    if (resetGraphics) resetGraphics();
    std::lock_guard lock(gMutex);
    gRegistration = {};
}
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
