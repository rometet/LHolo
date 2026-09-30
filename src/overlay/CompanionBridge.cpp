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
    void*       owner{};
    unsigned    hotkey{};
    DrawFn      drawGui{};
    DrawFn      drawHud{};
    HudNeededFn hudNeeded{};
    StateFn     stateChanged{};
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

bool registerProvider(
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
    gRegistration = Registration{
        owner, hotkey, drawGui, drawHud, hudNeeded, stateChanged
    };
    gVisible.store(false, std::memory_order_release);
    gRegistered.store(true, std::memory_order_release);
    logger().info("Companion GUI bridge v2 registered");
    return true;
}

bool unregisterProvider(void* owner) noexcept {
    // Unregistering from inside a provider callback would wait for the current
    // callback's own reader lease forever. Tell the provider to keep its DLL
    // loaded and retry from a safe lifecycle point.
    if (gReaderDepth != 0) return false;

    StateFn stateChanged{};
    bool wasVisible{};
    {
        std::lock_guard lock(gMutex);
        if (!gRegistered.load(std::memory_order_acquire)) return true;
        if (!owner || gRegistration.owner != owner) return false;
        gRegistered.store(false, std::memory_order_release);
        wasVisible = gVisible.exchange(false, std::memory_order_acq_rel);
        stateChanged = gRegistration.stateChanged;
    }

    waitForReaders();
    if (wasVisible && stateChanged) stateChanged(false);

    {
        std::lock_guard lock(gMutex);
        if (gRegistration.owner == owner) gRegistration = {};
    }
    logger().info("Companion GUI bridge v2 unregistered");
    return true;
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
    if (!lease.active || !isVisible() || !lease.registration.drawGui) return;
    lease.registration.drawGui(imguiContext);
}

void drawHud(void* imguiContext) noexcept {
    auto lease = acquireCallbacks();
    if (!lease.active || isVisible() || !lease.registration.drawHud) return;
    if (lease.registration.hudNeeded && !lease.registration.hudNeeded()) return;
    lease.registration.drawHud(imguiContext);
}

void shutdown() noexcept {
    {
        std::lock_guard lock(gMutex);
        gRegistered.store(false, std::memory_order_release);
        gVisible.store(false, std::memory_order_release);
    }
    waitForReaders();
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
    return lholo::overlay::companion::registerProvider(
        owner, hotkey, drawGui, drawHud, hudNeeded, stateChanged,
        imguiVersion, ioSize, styleSize, drawVertSize
    );
}
extern "C" __declspec(dllexport) bool __cdecl lholo_unregister_companion_gui_v2(
    void* owner
) noexcept {
    return lholo::overlay::companion::unregisterProvider(owner);
}

extern "C" __declspec(dllexport) bool __cdecl lholo_companion_gui_registered_v2(
    void* owner
) noexcept {
    std::lock_guard lock(lholo::overlay::companion::gMutex);
    return lholo::overlay::companion::gRegistered.load(std::memory_order_acquire)
        && lholo::overlay::companion::gRegistration.owner == owner;
}

extern "C" __declspec(dllexport) void __cdecl lholo_close_companion_gui_v2() noexcept {
    lholo::overlay::companion::close();
}

extern "C" __declspec(dllexport) bool __cdecl lholo_menu_input_captured_v2() noexcept {
    return lholo::overlay::companion::inputCaptured();
}
