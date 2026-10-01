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

#include "overlay/ImGuiOverlay.h"
#include "overlay/OverlayFonts.h"
#include "app/NativeCallbackBoundary.h"
#include "app/ScopeExit.h"
#include "overlay/ImGuiFrameRecovery.h"
#include "overlay/OverlayResourceGate.h"
#include "overlay/WindowCursorRestore.h"
#include "overlay/OverlayInstallRetry.h"
#include "overlay/NativeHookBinding.h"
#include "overlay/CompanionBridge.h"

#include <Windows.h>

#define D3D12_FEATURE_DATA_D3D12_OPTIONS D3D12_FEATURE_DATA_D3D12_OPTIONS_LEGACY
#define D3D12_FEATURE_DATA_ARCHITECTURE D3D12_FEATURE_DATA_ARCHITECTURE_LEGACY
#define D3D12_RAYTRACING_GEOMETRY_DESC D3D12_RAYTRACING_GEOMETRY_DESC_LEGACY
#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#undef D3D12_FEATURE_DATA_D3D12_OPTIONS
#undef D3D12_FEATURE_DATA_ARCHITECTURE
#undef D3D12_RAYTRACING_GEOMETRY_DESC

#include "overlay/D3D12QueueBinding.h"

#include <MinHook.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <imgui.h>

#include <atomic>
#include <array>
#include <cfloat>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>

#include "input/MenuInputGuard.h"
#include "plugin/LHolo.h"
#include "structure/StructureLoader.h"
#include "ui/FluentTheme.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace lholo::overlay {
namespace {

using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(__stdcall*)(IDXGISwapChain1*, UINT, UINT, DXGI_PRESENT_PARAMETERS const*);
using ResizeBuffersFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ResizeBuffers1Fn = HRESULT(__stdcall*)(
    IDXGISwapChain3*,
    UINT,
    UINT,
    UINT,
    DXGI_FORMAT,
    UINT,
    UINT const*,
    IUnknown* const*
);
using ExecuteCommandListsFn = void(__stdcall*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

PresentFn             gOriginalPresent{};
Present1Fn            gOriginalPresent1{};
ResizeBuffersFn       gOriginalResizeBuffers{};
ResizeBuffers1Fn      gOriginalResizeBuffers1{};
ExecuteCommandListsFn gOriginalExecuteCommandLists{};

detail::NativeHookBinding gPresentTarget;
detail::NativeHookBinding gPresent1Target;
detail::NativeHookBinding gResizeTarget;
detail::NativeHookBinding gResize1Target;
detail::NativeHookBinding gExecuteTarget;

ID3D11Device*        gDevice{};
ID3D11DeviceContext* gDeviceContext{};
ID3D11On12Device*    gDevice11On12{};
detail::D3D12QueueBinding gGameQueue;
// Weak identity of the swap chain currently owned by LHolo. Access is guarded
// by gResourceMutex; retaining it avoids extending the game's COM lifetime.
IDXGISwapChain*      gActiveSwapChain{};

HWND    gWindow{};
std::atomic<WNDPROC> gOriginalWndProc{};
std::atomic_bool gInstalled{false};
std::atomic_bool gExecuteHookInstalled{false};
std::atomic_bool gShuttingDown{false};
std::atomic_bool gRendering{false};
std::atomic_uint gHookCallbacks{};
thread_local unsigned int gHookCallbackDepth{};
thread_local char const*  gHookCallbackKind{"none"};
thread_local UINT         gHookCallbackMessage{};
std::atomic_ullong gGraphicsResumeAt{};
std::mutex       gResourceMutex;
// Render holds resources before ImGui. WndProc only takes this context lock
// around the backend handler and releases it before forwarding to Minecraft.
// Recursive acquisition permits Win32's synchronous capture-change messages.
std::recursive_mutex gImGuiMutex;
std::mutex       gInputStateMutex;
std::mutex       gInstallMutex;
std::atomic_ullong gInstallRetryAt{};
std::atomic_bool gImGuiInitialized{};
bool             gGraphicsInitialized{};
bool             gGuiVisibleLastFrame{};
std::atomic_bool gMouseHandoffActive{};
// Cursor ownership while the menu is open.
//
// The mods in this ecosystem decide "a UI owns the mouse" from GetCursorInfo():
// a displayed OS cursor means somebody has opened a UI, a hidden one means the
// player is in gameplay and the game holds the mouse. ImGui's software cursor
// breaks that contract - its Win32 backend calls SetCursor(nullptr) on every
// frame, which removes the cursor from the screen, so other mods keep re-centring
// a cursor they believe the game still owns (ChiyanMap clamps it to the client
// centre, freezing the mouse over our menu). The menu therefore draws with the
// native cursor and holds the ShowCursor display counter at >= 0 while visible.
//
// ShowCursor is per-thread state, so both halves run on the window thread via
// these messages. LHolo only ever releases the increments it forced itself, so
// the counter Minecraft owns ends up exactly where it was.
constexpr UINT kMsgRestoreNativeCursor = WM_APP + 0x101;
constexpr UINT kMsgAcquireMenuCursor   = WM_APP + 0x102;
constexpr UINT kMsgRestoreGameMouse    = WM_APP + 0x103;
std::atomic_int gMenuCursorShowCount{};
std::array<bool, 256> gGameKeysDown{};
std::array<bool, 5>   gGameMouseButtonsDown{};
std::atomic_bool      gConsumeEscapeRelease{false};

struct HookCallbackGuard {
    char const* previousKind{};
    UINT previousMessage{};

    explicit HookCallbackGuard(char const* kind, UINT message = 0) noexcept
        : previousKind(gHookCallbackKind), previousMessage(gHookCallbackMessage) {
        gHookCallbackKind = kind;
        gHookCallbackMessage = message;
        ++gHookCallbackDepth;
        gHookCallbacks.fetch_add(1, std::memory_order_acq_rel);
    }
    HookCallbackGuard(HookCallbackGuard const&) = delete;
    HookCallbackGuard& operator=(HookCallbackGuard const&) = delete;
    ~HookCallbackGuard() {
        gHookCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        --gHookCallbackDepth;
        gHookCallbackKind = previousKind;
        gHookCallbackMessage = previousMessage;
    }
};

bool insideHookCallback() noexcept {
    return gHookCallbackDepth != 0;
}

void waitForHookCallbacks(unsigned allowedCurrentThreadDepth = 0) noexcept {
    // Hooks are disabled before this is called, so the count converges to the
    // callbacks already executing on this thread. During normal teardown that
    // value is zero. During process-exit WM_DESTROY teardown, the current WndProc
    // is intentionally allowed to remain until the original WndProc returns.
    for (;;) {
        while (gHookCallbacks.load(std::memory_order_acquire) > allowedCurrentThreadDepth) {
            std::this_thread::yield();
        }
        std::this_thread::yield();
        if (gHookCallbacks.load(std::memory_order_acquire) <= allowedCurrentThreadDepth) return;
    }
}

constexpr ULONGLONG kFullscreenGraphicsResumeDelayMs = 750;
constexpr ULONGLONG kResizeGraphicsResumeDelayMs     = 100;
constexpr ULONGLONG kInstallRetryIntervalMs          = 1000;
constexpr size_t    kPresentVtableIndex              = 8;
constexpr size_t    kResizeBuffersVtableIndex        = 13;
constexpr size_t    kPresent1VtableIndex             = 22;
constexpr size_t    kResizeBuffers1VtableIndex       = 39;
constexpr size_t    kExecuteCommandListsVtableIndex  = 10;

auto& logger() { return LHolo::getInstance().getSelf().getLogger(); }

bool anyMenuVisible() {
    return structure::isGuiVisible() || companion::isVisible();
}

bool anyMenuInputCaptured() {
    return structure::isMenuInputCaptured() || companion::isVisible();
}

void logGraphicsFailure(IDXGISwapChain* swapChain, char const* operation, HRESULT result) {
    HRESULT removedReason = S_OK;
    ID3D12Device* device12{};
    if (swapChain && SUCCEEDED(swapChain->GetDevice(
            __uuidof(ID3D12Device),
            reinterpret_cast<void**>(&device12)
        ))) {
        removedReason = device12->GetDeviceRemovedReason();
        device12->Release();
    }
    logger().error(
        "ImGui graphics call {} failed: HRESULT=0x{:08X}, deviceRemovedReason=0x{:08X}",
        operation,
        static_cast<unsigned int>(result),
        static_cast<unsigned int>(removedReason)
    );
}

HWND findProcessWindow() {
    struct Search { DWORD pid; HWND result; } search{GetCurrentProcessId(), nullptr};
    EnumWindows(
        [](HWND window, LPARAM parameter) -> BOOL {
            auto& state = *reinterpret_cast<Search*>(parameter);
            DWORD pid{};
            GetWindowThreadProcessId(window, &pid);
            if (pid == state.pid && IsWindowVisible(window) && GetWindow(window, GW_OWNER) == nullptr) {
                state.result = window;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search)
    );
    return search.result;
}

void releaseGraphicsBackend() {
    std::lock_guard imguiLock(gImGuiMutex);
    // A v3 companion owns its own ImGui/DX11 backend on the same device/context.
    // Tear it down before LHolo releases or recreates the shared graphics device.
    companion::resetGraphics();
    if (gGraphicsInitialized) {
        ImGui_ImplDX11_Shutdown();
        gGraphicsInitialized = false;
    }
    if (gDeviceContext) {
        ID3D11RenderTargetView* empty{};
        gDeviceContext->OMSetRenderTargets(1, &empty, nullptr);
        gDeviceContext->ClearState();
        gDeviceContext->Flush();
    }
    if (gDevice11On12) gDevice11On12->Release();
    if (gDeviceContext) gDeviceContext->Release();
    if (gDevice) gDevice->Release();
    gDevice11On12 = nullptr;
    gDeviceContext = nullptr;
    gDevice = nullptr;
}

bool canUseSwapChainLocked(IDXGISwapChain* swapChain) {
    if (!swapChain) return false;
    if (!gActiveSwapChain || gActiveSwapChain == swapChain) return true;
    if (gGraphicsInitialized || !gWindow) return false;

    // A fullscreen/device transition may replace the swap-chain object while
    // retaining the game window. Permit that explicit handoff, but never let
    // a composition/off-screen chain from another mod claim the overlay.
    DXGI_SWAP_CHAIN_DESC description{};
    return SUCCEEDED(swapChain->GetDesc(&description))
        && description.OutputWindow == gWindow;
}

bool retireGraphicsDeviceLossLocked(IDXGISwapChain* swapChain, HRESULT result) {
    if (!canUseSwapChainLocked(swapChain)) return false;
    if (!gGameQueue.retireAfterDeviceLoss(result, [] { releaseGraphicsBackend(); })) return false;
    gActiveSwapChain = nullptr;
    return true;
}

void handlePresentationResult(IDXGISwapChain* swapChain, HRESULT result, char const* operation) {
    if (result != DXGI_ERROR_DEVICE_REMOVED && result != DXGI_ERROR_DEVICE_RESET) return;
    app::invokeNativeCallback([&] {
        std::lock_guard lock(gResourceMutex);
        if (!gShuttingDown.load(std::memory_order_acquire)
            && retireGraphicsDeviceLossLocked(swapChain, result)) {
            logGraphicsFailure(swapChain, operation, result);
        }
    }, [](char const* reason) noexcept {
        app::reportNativeCallbackFailure("graphics device retirement", reason);
    });
}

// Callers must hold gResourceMutex across both helpers and the original DXGI
// resize call so Present cannot rebuild against a swap chain mid-transition.
void prepareForSwapChainResizeLocked() {
    if (gGraphicsInitialized) releaseGraphicsBackend();
}

void deferGraphicsResumeAfterSwapChainResizeLocked() {
    // Window-edge dragging can issue a burst of resizes. Debounce backend
    // recreation and let the first stable Present rebuild it lazily. Never
    // shorten the longer suspension already scheduled by an F11 transition.
    auto const resizeResumeAt = GetTickCount64() + kResizeGraphicsResumeDelayMs;
    auto const currentResumeAt = gGraphicsResumeAt.load(std::memory_order_acquire);
    if (currentResumeAt < resizeResumeAt) {
        gGraphicsResumeAt.store(resizeResumeAt, std::memory_order_release);
    }
}

void loadFonts() {
    loadOverlayFonts(*ImGui::GetIO().Fonts, {
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\meiryo.ttc",
        "C:\\Windows\\Fonts\\seguisym.ttf"
    });
}

LRESULT forwardToGame(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    {
        std::lock_guard lock(gInputStateMutex);
        if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && wParam < gGameKeysDown.size()) {
            gGameKeysDown[static_cast<std::size_t>(wParam)] = true;
        } else if ((message == WM_KEYUP || message == WM_SYSKEYUP) && wParam < gGameKeysDown.size()) {
            gGameKeysDown[static_cast<std::size_t>(wParam)] = false;
        }
        switch (message) {
        case WM_LBUTTONDOWN: gGameMouseButtonsDown[0] = true; break;
        case WM_LBUTTONUP: gGameMouseButtonsDown[0] = false; break;
        case WM_RBUTTONDOWN: gGameMouseButtonsDown[1] = true; break;
        case WM_RBUTTONUP: gGameMouseButtonsDown[1] = false; break;
        case WM_MBUTTONDOWN: gGameMouseButtonsDown[2] = true; break;
        case WM_MBUTTONUP: gGameMouseButtonsDown[2] = false; break;
        case WM_XBUTTONDOWN: gGameMouseButtonsDown[GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? 3 : 4] = true; break;
        case WM_XBUTTONUP: gGameMouseButtonsDown[GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? 3 : 4] = false; break;
        default: break;
        }
    }
    return gOriginalWndProc ? CallWindowProcW(gOriginalWndProc, window, message, wParam, lParam)
                            : DefWindowProcW(window, message, wParam, lParam);
}

void releaseGameInput(HWND window) {
    // Minecraft has already seen these down events. Send matching releases
    // before the menu starts swallowing input, otherwise movement/use remains
    // latched after the physical key is released while ImGui is open.
    input::MenuInputHandoffScope inputHandoff;
    std::array<bool, 256> keysDown{};
    std::array<bool, 5> mouseButtonsDown{};
    {
        std::lock_guard lock(gInputStateMutex);
        keysDown = gGameKeysDown;
        mouseButtonsDown = gGameMouseButtonsDown;
    }
    for (std::size_t key = 0; key < keysDown.size(); ++key) {
        if (!keysDown[key]) continue;
        auto const virtualKey = static_cast<UINT>(key);
        auto scanCode = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC);
        auto const extended = virtualKey == VK_LEFT || virtualKey == VK_UP
            || virtualKey == VK_RIGHT || virtualKey == VK_DOWN
            || virtualKey == VK_PRIOR || virtualKey == VK_NEXT
            || virtualKey == VK_END || virtualKey == VK_HOME
            || virtualKey == VK_INSERT || virtualKey == VK_DELETE
            || virtualKey == VK_DIVIDE || virtualKey == VK_NUMLOCK;
        LPARAM keyUp = 1 | (static_cast<LPARAM>(scanCode) << 16) | (1LL << 30) | (1LL << 31);
        if (extended) keyUp |= 1LL << 24;
        auto const systemKey = virtualKey == VK_MENU || virtualKey == VK_LMENU || virtualKey == VK_RMENU;
        forwardToGame(window, systemKey ? WM_SYSKEYUP : WM_KEYUP, virtualKey, keyUp);
    }

    POINT cursor{};
    GetCursorPos(&cursor);
    ScreenToClient(window, &cursor);
    auto const mousePosition = MAKELPARAM(cursor.x, cursor.y);
    constexpr std::array<UINT, 5> upMessages{
        WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP, WM_XBUTTONUP, WM_XBUTTONUP
    };
    for (std::size_t button = 0; button < mouseButtonsDown.size(); ++button) {
        if (!mouseButtonsDown[button]) continue;
        WPARAM buttonParam{};
        if (button == 3) buttonParam = MAKEWPARAM(0, XBUTTON1);
        if (button == 4) buttonParam = MAKEWPARAM(0, XBUTTON2);
        forwardToGame(window, upMessages[button], buttonParam, mousePosition);
    }

}

bool confineMouseToClientCenter(HWND window) {
    RECT clientRect{};
    if (!window || !GetClientRect(window, &clientRect)) return false;
    POINT topLeft{clientRect.left, clientRect.top};
    POINT bottomRight{clientRect.right, clientRect.bottom};
    if (!ClientToScreen(window, &topLeft) || !ClientToScreen(window, &bottomRight)) return false;
    RECT screenRect{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};

    // Never let the hidden native cursor touch a Windows hot edge while
    // gameplay is reclaiming relative mouse input. In borderless/maximized
    // mode the client rect can end exactly on the auto-hide taskbar trigger.
    constexpr LONG kHotEdgeInset = 2;
    if (screenRect.right - screenRect.left > kHotEdgeInset * 2) {
        screenRect.left += kHotEdgeInset;
        screenRect.right -= kHotEdgeInset;
    }
    if (screenRect.bottom - screenRect.top > kHotEdgeInset * 2) {
        screenRect.top += kHotEdgeInset;
        screenRect.bottom -= kHotEdgeInset;
    }

    if (!ClipCursor(&screenRect)) return false;
    return SetCursorPos(
        (screenRect.left + screenRect.right) / 2,
        (screenRect.top + screenRect.bottom) / 2
    ) != FALSE;
}

void restoreGameMouseOwnership(HWND window) {
    if (!window || gShuttingDown.load(std::memory_order_acquire) || anyMenuVisible()) return;

    auto client = ll::service::getClientInstance();
    if (!client) return;
    auto const screen = client->getTopScreenName();
    if (screen != "hud_screen" && screen != "in_game_play_screen") return;

    // Center/clip first so grabMouse() cannot inherit the absolute menu cursor
    // position. Explicitly returning ownership fixes the case where Bedrock
    // keeps receiving raw look input while the Windows cursor itself remains
    // free and drifts into the taskbar hot edge.
    confineMouseToClientCenter(window);
    client->grabMouse();
}

void prepareMouseHandoff(HWND window) {
    if (!window) return;

    // ImGui keeps button/position state independently from Win32. Clear it
    // before Minecraft resumes relative mouse input so the closing click can
    // never survive into the first gameplay frame.
    if (gImGuiInitialized && ImGui::GetCurrentContext()) {
        auto& io = ImGui::GetIO();
        for (bool& down : io.MouseDown) down = false;
        io.MouseWheel = 0.0f;
        io.MouseWheelH = 0.0f;
        io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
    }
    {
        std::lock_guard lock(gInputStateMutex);
        gGameMouseButtonsDown.fill(false);
    }

    // A full-screen menu can leave the absolute OS cursor anywhere. Bedrock
    // converts that absolute position back to relative-look input when it
    // captures the mouse again; centering first prevents a one-frame camera
    // jump proportional to the distance from the menu button to the center.
    if (confineMouseToClientCenter(window)) {
        gMouseHandoffActive.store(true, std::memory_order_release);
    }
}

void maintainMouseHandoff(HWND window) {
    if (!gMouseHandoffActive.load(std::memory_order_acquire) || !window) return;
    if (!structure::isInputTransitionBlocked()) {
        // The menu transition is complete. Do not rely on a later Bedrock
        // frame to recapture the mouse. Hand the final grab back to the
        // window thread, matching the rest of the cursor ownership path.
        PostMessageW(window, kMsgRestoreGameMouse, 0, 0);
        gMouseHandoffActive.store(false, std::memory_order_release);
        return;
    }
    confineMouseToClientCenter(window);
}

// -- window thread only ------------------------------------------------------
// Raise the ShowCursor display counter until the OS cursor can be shown.
// ShowCursor returns the counter AFTER the call, so a result above 0 means the
// cursor was already visible and this probe increment has to be undone - that
// keeps repeated calls idempotent instead of drifting upward frame after frame.
void acquireMenuCursor() {
    if (ShowCursor(TRUE) > 0) {
        ShowCursor(FALSE);
        return;
    }
    gMenuCursorShowCount.fetch_add(1, std::memory_order_acq_rel);
}

// Drop exactly the increments acquireMenuCursor() forced. Minecraft's own
// negative counter is never touched, so gameplay still hides the cursor.
void releaseMenuCursor() {
    while (gMenuCursorShowCount.load(std::memory_order_acquire) > 0) {
        gMenuCursorShowCount.fetch_sub(1, std::memory_order_acq_rel);
        ShowCursor(FALSE);
    }
}

bool isMenuInputMessage(UINT message) {
    switch (message) {
    case WM_INPUT:
    case WM_INPUT_DEVICE_CHANGE:
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_CHAR:
        return true;
    default:
        return false;
    }
}

bool isFullscreenKeyMessage(UINT message, WPARAM wParam) {
    return wParam == VK_F11
        && (message == WM_KEYDOWN || message == WM_KEYUP
            || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP);
}

LRESULT consumeMenuInputMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INPUT) {
        // Foreground RIM_INPUT must reach DefWindowProc for User32 cleanup,
        // but must not be forwarded to Minecraft's original WndProc.
        return DefWindowProcW(window, message, wParam, lParam);
    }
    return 1;
}

std::optional<LRESULT> handleWindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    // Shutdown must still be able to return its ShowCursor increments on this
    // owning thread before restoring the original window procedure.
    if (message == kMsgRestoreNativeCursor) {
        releaseMenuCursor();
        ::SetCursor(::LoadCursorW(nullptr, IDC_ARROW));
        return 0;
    }
    if (gShuttingDown.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    if (message == kMsgAcquireMenuCursor) {
        acquireMenuCursor();
        return 0;
    }
    if (message == kMsgRestoreGameMouse) {
        restoreGameMouseOwnership(window);
        return 0;
    }
    if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && wParam == FALSE)) {
        structure::resetHotkeyState();
        gMouseHandoffActive.store(false, std::memory_order_release);
        releaseMenuCursor();
        ClipCursor(nullptr);
    }
    if (!gShuttingDown.load(std::memory_order_acquire)
        && message == WM_KEYDOWN && wParam == VK_F11
        && (lParam & (1LL << 30)) == 0) {
        {
            std::lock_guard lock(gResourceMutex);
            releaseGraphicsBackend();
            gGraphicsResumeAt.store(
                GetTickCount64() + kFullscreenGraphicsResumeDelayMs,
                std::memory_order_release
            );
        }
        logger().info("ImGui graphics backend suspended for fullscreen transition");
    }

    auto const menuWasVisible = anyMenuVisible();
    auto const lholoWasVisible = structure::isGuiVisible();

    if (!gShuttingDown.load(std::memory_order_acquire) && gImGuiInitialized
        && (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)) {
        auto const repeated = (lParam & (1LL << 30)) != 0;
        if (companion::handleHotkeyKeyDown(
                static_cast<unsigned int>(wParam), repeated)) {
            if (companion::isVisible() && structure::isGuiVisible()) {
                structure::requestOpenGui();
            }
            auto const menuVisible = anyMenuVisible();
            if (!menuWasVisible && menuVisible) releaseGameInput(window);
            if (menuWasVisible && !menuVisible) confineMouseToClientCenter(window);
            return 1;
        }
        if (structure::handleGuiHotkeyKeyDown(static_cast<unsigned int>(wParam))) {
            if (!lholoWasVisible && structure::isGuiVisible() && companion::isVisible()) {
                companion::close();
            }
            auto const menuVisible = anyMenuVisible();
            if (!menuWasVisible && menuVisible) releaseGameInput(window);
            if (menuWasVisible && !menuVisible) confineMouseToClientCenter(window);
            return 1;
        }
    }

    if (!gShuttingDown.load(std::memory_order_acquire) && gImGuiInitialized
        && (message == WM_KEYUP || message == WM_SYSKEYUP)) {
        if (companion::handleHotkeyKeyUp(static_cast<unsigned int>(wParam))) return 1;
        if (structure::handleGuiHotkeyKeyUp(static_cast<unsigned int>(wParam))) return 1;
    }
    if ((message == WM_KEYUP || message == WM_SYSKEYUP) && wParam == VK_ESCAPE
        && gConsumeEscapeRelease.exchange(false, std::memory_order_acq_rel)) {
        return 1;
    }

    // Mouse middle/side buttons can be bound as LHolo hotkeys too.
    if (!gShuttingDown.load(std::memory_order_acquire) && gImGuiInitialized) {
        if (message == WM_MOUSEWHEEL && structure::handleProjectionOffsetWheel(
            GET_WHEEL_DELTA_WPARAM(wParam))) {
            return 1;
        }
        unsigned int mouseKey = 0;
        switch (message) {
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
            mouseKey = VK_MBUTTON;
            break;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
            mouseKey = GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
            break;
        default:
            break;
        }
        if (mouseKey != 0) {
            if (message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN) {
                if (structure::handleGuiHotkeyKeyDown(mouseKey)) {
                    if (!lholoWasVisible && structure::isGuiVisible() && companion::isVisible()) {
                        companion::close();
                    }
                    auto const menuVisible = anyMenuVisible();
                    if (!menuWasVisible && menuVisible) releaseGameInput(window);
                    if (menuWasVisible && !menuVisible) confineMouseToClientCenter(window);
                    return 1;
                }
            } else if (structure::handleGuiHotkeyKeyUp(mouseKey)) {
                return 1;
            }
        }
    }

    if (!gShuttingDown.load(std::memory_order_acquire)
        && gImGuiInitialized && anyMenuVisible()) {
        gMouseHandoffActive.store(false, std::memory_order_release);
        ClipCursor(nullptr);
        if (companion::isVisible() && companion::usesIndependentRenderer()) {
            companion::forwardWindowMessage(
                window, message,
                static_cast<std::uintptr_t>(wParam),
                static_cast<std::intptr_t>(lParam)
            );
        } else {
            std::lock_guard imguiLock(gImGuiMutex);
            if (gImGuiInitialized && !gShuttingDown.load(std::memory_order_acquire)) {
                ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
            }
        }
        if (isFullscreenKeyMessage(message, wParam)) {
            return std::nullopt;
        }
        if (message == WM_KEYDOWN && wParam == VK_ESCAPE) {
            gConsumeEscapeRelease.store(true, std::memory_order_release);
            if (structure::isGuiVisible()) structure::requestOpenGui();
            else companion::close();
            if (!anyMenuVisible()) confineMouseToClientCenter(window);
            return 1;
        }
        if (isMenuInputMessage(message)) {
            return consumeMenuInputMessage(window, message, wParam, lParam);
        }
        if (message == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT) return 1;
    }
    if (anyMenuInputCaptured()
        && isMenuInputMessage(message)
        && !isFullscreenKeyMessage(message, wParam)) {
        return consumeMenuInputMessage(window, message, wParam, lParam);
    }
    return std::nullopt;
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    HookCallbackGuard callbackGuard{"WndProc", message};
    std::optional<LRESULT> handled;
    auto const succeeded = app::invokeNativeCallback([&] {
        handled = handleWindowMessage(window, message, wParam, lParam);
    }, [](char const* reason) noexcept {
        app::reportNativeCallbackFailure("window input", reason);
    });
    if (succeeded && handled) return *handled;
    // Forward the real message once, outside the LHolo exception boundary.
    return forwardToGame(window, message, wParam, lParam);
}

void executeCommandListsHook(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists) {
    HookCallbackGuard callbackGuard{"ExecuteCommandLists"};
    if (!gShuttingDown.load(std::memory_order_acquire)
        && queue
        && !gGameQueue.captured()
        && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
        std::lock_guard lock(gResourceMutex);
        gGameQueue.capture(*queue);
    }
    gOriginalExecuteCommandLists(queue, count, lists);
}

bool initializeImGui(IDXGISwapChain* swapChain) {
    if (gImGuiInitialized && gGraphicsInitialized) return true;
    if (GetTickCount64() < gGraphicsResumeAt.load(std::memory_order_acquire)) return false;

    // Every failed attempt starts the next Present from a known empty graphics
    // state. This also cleans partial COM objects left by a failed API call.
    releaseGraphicsBackend();
    if (SUCCEEDED(swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&gDevice)))) {
        gDevice->GetImmediateContext(&gDeviceContext);
    } else {
        Microsoft::WRL::ComPtr<ID3D12Device> device12;
        if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&device12)))) return false;
        // A different mod/device may have executed first. Keep the target
        // device while waiting, and accept only its next direct queue. The old
        // backend has already been flushed above before the capture flag clears.
        if (!gGameQueue.bindDevice(*device12.Get())) return false;
        IUnknown* queues[]{gGameQueue.get()};
        auto const result = D3D11On12CreateDevice(
            device12.Get(),
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,
            0,
            queues,
            1,
            0,
            &gDevice,
            &gDeviceContext,
            nullptr
        );
        if (FAILED(result) || !gDevice) {
            releaseGraphicsBackend();
            return false;
        }
        if (FAILED(gDevice->QueryInterface(__uuidof(ID3D11On12Device), reinterpret_cast<void**>(&gDevice11On12)))) {
            releaseGraphicsBackend();
            return false;
        }
    }

    DXGI_SWAP_CHAIN_DESC description{};
    if (FAILED(swapChain->GetDesc(&description))) {
        releaseGraphicsBackend();
        return false;
    }
    auto const window = description.OutputWindow ? description.OutputWindow : findProcessWindow();
    if (!window || (gWindow && window != gWindow)) {
        releaseGraphicsBackend();
        return false;
    }

    if (!gImGuiInitialized) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        app::ScopeExit rollbackContext([]() noexcept {
            if (gImGuiInitialized || !ImGui::GetCurrentContext()) return;
            if (ImGui::GetIO().BackendPlatformUserData) ImGui_ImplWin32_Shutdown();
            ui::resetFluentTheme();
            ImGui::DestroyContext();
        });
        ImGui::StyleColorsDark();
        loadFonts();
        if (!ImGui_ImplWin32_Init(window)) {
            ui::resetFluentTheme();
            ImGui::DestroyContext();
            releaseGraphicsBackend();
            return false;
        }
        gWindow = window;
        // Publish a forwarding target before installing our procedure: an
        // unrelated window message may enter it immediately on the UI thread.
        auto const forwardingWndProc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(gWindow, GWLP_WNDPROC));
        if (!forwardingWndProc) {
            logger().error("Could not read the game window procedure: Win32 error {}", GetLastError());
            ImGui_ImplWin32_Shutdown();
            ui::resetFluentTheme();
            ImGui::DestroyContext();
            gWindow = nullptr;
            releaseGraphicsBackend();
            return false;
        }
        gOriginalWndProc.store(forwardingWndProc, std::memory_order_release);
        SetLastError(0);
        auto const previousWndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(gWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(windowProc))
        );
        if (!previousWndProc) {
            logger().error("Could not subclass the game window: Win32 error {}", GetLastError());
            ImGui_ImplWin32_Shutdown();
            ui::resetFluentTheme();
            ImGui::DestroyContext();
            gWindow = nullptr;
            gOriginalWndProc.store(nullptr, std::memory_order_release);
            releaseGraphicsBackend();
            return false;
        }
        gOriginalWndProc.store(previousWndProc, std::memory_order_release);
        gImGuiInitialized = true;
        logger().info("Injected Dear ImGui overlay initialized");
    }
    if (!ImGui_ImplDX11_Init(gDevice, gDeviceContext)) {
        releaseGraphicsBackend();
        return false;
    }
    gGraphicsInitialized = true;
    gActiveSwapChain = swapChain;
    logger().info("ImGui graphics backend initialized");
    return true;
}

void render(IDXGISwapChain* swapChain) {
    if (gShuttingDown.load(std::memory_order_acquire)) return;
    if (gRendering.exchange(true, std::memory_order_acq_rel)) return;
    struct Reset { ~Reset() { gRendering.store(false, std::memory_order_release); } } reset;
    auto lock = detail::acquireLiveOverlayResources(gResourceMutex, gShuttingDown);
    if (!lock.owns_lock()) return;
    std::lock_guard imguiLock(gImGuiMutex);
    if (!canUseSwapChainLocked(swapChain)) return;

    // Initialize the backend and install the WndProc on the first usable
    // Present even while the menu is hidden. This matches ChiyanMap's proven
    // lifecycle: input must be ready before a hotkey can make the UI visible.
    // D3D12 startup may not have exposed its command queue yet, so a failed
    // attempt is intentionally retried on the next Present.
    if (!initializeImGui(swapChain)) return;
    structure::processPendingActions();

    auto const showLholoGui = structure::isGuiVisible();
    auto const showCompanionGui = companion::isVisible();
    auto const showGui = showLholoGui || showCompanionGui;
    auto const showHud = !showGui
        && (structure::hasHudInfo() || companion::hudNeeded());
    if (gGuiVisibleLastFrame != showGui) {
        if (!showGui) {
            prepareMouseHandoff(gWindow);
            PostMessageW(gWindow, kMsgRestoreNativeCursor, 0, 0);
        }
    }
    gGuiVisibleLastFrame = showGui;
    if (!showGui) maintainMouseHandoff(gWindow);
    // Keep the native cursor for the menu: ImGui's software cursor would hide
    // the OS one and make other mods believe the game still owns the mouse.
    // See the cursor-ownership note at the top of this file.
    ImGui::GetIO().MouseDrawCursor = false;
    auto const showHint = structure::actionHintActive();
    if (!showGui && !showHud && !showHint) return;

    if (showGui) {
        // Re-asserted every frame: a Minecraft screen transition, an alt-tab or
        // another overlay can hide the cursor again behind our back. The handler
        // is idempotent, so this never drifts the display counter.
        PostMessageW(gWindow, kMsgAcquireMenuCursor, 0, 0);
        ClipCursor(nullptr);
    }

    auto draw = [](ID3D11RenderTargetView* target) {
        gDeviceContext->OMSetRenderTargets(1, &target, nullptr);
        app::ScopeExit resetTarget([&]() noexcept {
            ID3D11RenderTargetView* empty{};
            gDeviceContext->OMSetRenderTargets(1, &empty, nullptr);
        });

        auto const companionVisible = companion::isVisible();
        auto const independentCompanion = companion::usesIndependentRenderer();

        if (companionVisible && independentCompanion) {
            // Bridge v3: Praxis owns a completely separate ImGui context, font
            // atlas and style. LHolo only lends the already-hooked render target
            // and D3D11 device/context for this frame.
            companion::renderIndependent(gDevice, gDeviceContext, gWindow, true);
        } else {
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            detail::ImGuiFrameRecovery frameRecovery;

            if (structure::isGuiVisible()) {
                structure::renderGui();
            } else if (companionVisible) {
                // Legacy bridge v2 fallback.
                companion::drawGui(ImGui::GetCurrentContext());
            } else {
                structure::renderHud();
                structure::renderMaterialHud();
                if (!independentCompanion)
                    companion::drawHud(ImGui::GetCurrentContext());
            }

            structure::renderActionHint();
            ImGui::Render();
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }

        if (!companionVisible && independentCompanion && companion::hudNeeded()) {
            companion::renderIndependent(gDevice, gDeviceContext, gWindow, false);
        }

        // Either menu may close itself from inside its draw callback.
        if (gGuiVisibleLastFrame && !anyMenuVisible()) {
            prepareMouseHandoff(gWindow);
            PostMessageW(gWindow, kMsgRestoreNativeCursor, 0, 0);
            gGuiVisibleLastFrame = false;
        }

    };

    if (gDevice11On12) {
        UINT index{};
        IDXGISwapChain3* swapChain3{};
        if (SUCCEEDED(swapChain->QueryInterface(
                __uuidof(IDXGISwapChain3),
                reinterpret_cast<void**>(&swapChain3)
            ))) {
            index = swapChain3->GetCurrentBackBufferIndex();
            swapChain3->Release();
        }

        ID3D12Resource* backBuffer{};
        auto const getBufferResult = swapChain->GetBuffer(
                index,
                __uuidof(ID3D12Resource),
                reinterpret_cast<void**>(&backBuffer)
            );
        if (FAILED(getBufferResult)) {
            retireGraphicsDeviceLossLocked(swapChain, getBufferResult);
            logGraphicsFailure(swapChain, "GetBuffer(D3D12)", getBufferResult);
            return;
        }
        ID3D11Resource* wrappedBuffer{};
        D3D11_RESOURCE_FLAGS resourceFlags{D3D11_BIND_RENDER_TARGET};
        auto const wrappedResult = gDevice11On12->CreateWrappedResource(
            backBuffer,
            &resourceFlags,
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_PRESENT,
            __uuidof(ID3D11Resource),
            reinterpret_cast<void**>(&wrappedBuffer)
        );
        backBuffer->Release();
        if (FAILED(wrappedResult) || !wrappedBuffer) {
            retireGraphicsDeviceLossLocked(swapChain, wrappedResult);
            logGraphicsFailure(swapChain, "CreateWrappedResource", wrappedResult);
            return;
        }
        app::ScopeExit releaseWrapped([&]() noexcept { wrappedBuffer->Release(); });

        ID3D11RenderTargetView* target{};
        auto const targetResult = gDevice->CreateRenderTargetView(wrappedBuffer, nullptr, &target);
        app::ScopeExit releaseTarget([&]() noexcept { if (target) target->Release(); });
        if (SUCCEEDED(targetResult) && target) {
            gDevice11On12->AcquireWrappedResources(&wrappedBuffer, 1);
            app::ScopeExit releaseAcquisition([&]() noexcept {
                gDevice11On12->ReleaseWrappedResources(&wrappedBuffer, 1);
                gDeviceContext->Flush();
            });
            draw(target);
        } else {
            retireGraphicsDeviceLossLocked(swapChain, targetResult);
            logGraphicsFailure(swapChain, "CreateRenderTargetView(D3D11On12)", targetResult);
        }
    } else {
        ID3D11Texture2D* backBuffer{};
        auto const getBufferResult = swapChain->GetBuffer(
            0,
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(&backBuffer)
        );
        if (FAILED(getBufferResult)) {
            retireGraphicsDeviceLossLocked(swapChain, getBufferResult);
            logGraphicsFailure(swapChain, "GetBuffer(D3D11)", getBufferResult);
            return;
        }
        ID3D11RenderTargetView* target{};
        auto const result = gDevice->CreateRenderTargetView(backBuffer, nullptr, &target);
        backBuffer->Release();
        app::ScopeExit releaseTarget([&]() noexcept { if (target) target->Release(); });
        if (SUCCEEDED(result) && target) {
            draw(target);
        } else {
            retireGraphicsDeviceLossLocked(swapChain, result);
            logGraphicsFailure(swapChain, "CreateRenderTargetView(D3D11)", result);
        }
    }
}

HRESULT __stdcall presentHook(IDXGISwapChain* swapChain, UINT interval, UINT flags) {
    HookCallbackGuard callbackGuard{"Present"};
    if (!gShuttingDown.load(std::memory_order_acquire)) {
        app::invokeNativeCallback([&] { render(swapChain); }, [](char const* reason) noexcept {
            app::reportNativeCallbackFailure("Present", reason);
        });
    }
    auto const result = gOriginalPresent(swapChain, interval, flags);
    handlePresentationResult(swapChain, result, "Present(device loss)");
    return result;
}

HRESULT __stdcall present1Hook(
    IDXGISwapChain1* swapChain,
    UINT interval,
    UINT flags,
    DXGI_PRESENT_PARAMETERS const* parameters
) {
    HookCallbackGuard callbackGuard{"Present1"};
    if (!gShuttingDown.load(std::memory_order_acquire)) {
        app::invokeNativeCallback([&] { render(swapChain); }, [](char const* reason) noexcept {
            app::reportNativeCallbackFailure("Present1", reason);
        });
    }
    auto const result = gOriginalPresent1(swapChain, interval, flags, parameters);
    handlePresentationResult(swapChain, result, "Present1(device loss)");
    return result;
}

HRESULT __stdcall resizeHook(
    IDXGISwapChain* swapChain,
    UINT count,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    UINT flags
) {
    HookCallbackGuard callbackGuard{"ResizeBuffers"};
    if (gShuttingDown.load(std::memory_order_acquire)) {
        return gOriginalResizeBuffers(swapChain, count, width, height, format, flags);
    }
    std::unique_lock lock(gResourceMutex);
    if (gActiveSwapChain != swapChain) {
        lock.unlock();
        return gOriginalResizeBuffers(swapChain, count, width, height, format, flags);
    }

    // ResizeBuffers requires every reference to the old buffers to be gone.
    // Although LHolo creates its wrapped back buffer and RTV per frame, the
    // D3D11On12 context may still retain state from the last submission. Use
    // the same full graphics-backend teardown as the proven F11 path, while
    // preserving the ImGui context, Win32 backend and menu state.
    prepareForSwapChainResizeLocked();

    auto const result = gOriginalResizeBuffers(swapChain, count, width, height, format, flags);
    deferGraphicsResumeAfterSwapChainResizeLocked();
    if (FAILED(result)) {
        app::invokeNativeCallback([&] {
            retireGraphicsDeviceLossLocked(swapChain, result);
            logGraphicsFailure(swapChain, "ResizeBuffers", result);
        },
            [](char const* reason) noexcept { app::reportNativeCallbackFailure("resize diagnostic", reason); });
    }
    return result;
}

HRESULT __stdcall resize1Hook(
    IDXGISwapChain3* swapChain,
    UINT count,
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    UINT flags,
    UINT const* creationNodeMask,
    IUnknown* const* presentQueue
) {
    HookCallbackGuard callbackGuard{"ResizeBuffers1"};
    if (gShuttingDown.load(std::memory_order_acquire)) {
        return gOriginalResizeBuffers1(
            swapChain,
            count,
            width,
            height,
            format,
            flags,
            creationNodeMask,
            presentQueue
        );
    }
    std::unique_lock lock(gResourceMutex);
    if (gActiveSwapChain != static_cast<IDXGISwapChain*>(swapChain)) {
        lock.unlock();
        return gOriginalResizeBuffers1(
            swapChain,
            count,
            width,
            height,
            format,
            flags,
            creationNodeMask,
            presentQueue
        );
    }
    prepareForSwapChainResizeLocked();
    auto const result = gOriginalResizeBuffers1(
        swapChain,
        count,
        width,
        height,
        format,
        flags,
        creationNodeMask,
        presentQueue
    );
    deferGraphicsResumeAfterSwapChainResizeLocked();
    if (FAILED(result)) {
        app::invokeNativeCallback([&] {
            retireGraphicsDeviceLossLocked(swapChain, result);
            logGraphicsFailure(swapChain, "ResizeBuffers1", result);
        },
            [](char const* reason) noexcept { app::reportNativeCallbackFailure("resize diagnostic", reason); });
    }
    return result;
}

bool installHook(detail::NativeHookBinding& binding, void* target, void* detour, void** original) {
    if (!target) return false;
    auto const created = binding.create(target, detour, original);
    if (created != MH_OK) {
        logger().error("Overlay hook creation failed: {}", static_cast<int>(created));
        return false;
    }
    auto const enabled = binding.enable();
    if (enabled != MH_OK) {
        logger().error("Overlay hook enable refused/failed: {}", static_cast<int>(enabled));
        return false; // Retain the owned target for rollback.
    }
    return true;
}

bool disableHook(detail::NativeHookBinding& binding) {
    if (binding.disable()) return true;
    logger().error("Overlay hook disable refused/failed: target={} patchOwned={} native status={}; retaining trampoline",
        binding.target(), binding.currentPatchOwned(), static_cast<int>(binding.lastStatus()));
    return false;
}

bool removeHook(detail::NativeHookBinding& binding) {
    if (!binding.remove()) {
        logger().error("Overlay hook removal refused/failed: target={} enabled={} native status={}",
            binding.target(), binding.enabled(), static_cast<int>(binding.lastStatus()));
        return false;
    }
    return true;
}

bool tryInstallExecuteHook() {
    HRESULT discovered = S_OK;
    // Keep discovery's COM objects alive until CreateHook/EnableHook completes.
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    bool const installed = gExecuteTarget.installOrRetry(
        reinterpret_cast<void*>(executeCommandListsHook), reinterpret_cast<void**>(&gOriginalExecuteCommandLists), [&]() -> void* {
            discovered = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
            if (FAILED(discovered)) return nullptr;
            D3D12_COMMAND_QUEUE_DESC description{};
            description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            discovered = device->CreateCommandQueue(&description, IID_PPV_ARGS(&queue));
            if (FAILED(discovered)) return nullptr;
            return (*reinterpret_cast<void***>(queue.Get()))[kExecuteCommandListsVtableIndex];
        }
    );
    gExecuteHookInstalled.store(installed, std::memory_order_release);
    if (!installed) {
        logger().warn("D3D12 Execute hook deferred: discovery HRESULT=0x{:08X} native status={}; retrying",
            static_cast<unsigned long>(discovered), static_cast<int>(gExecuteTarget.lastStatus()));
    }
    return installed;
}

} // namespace

namespace {

bool shutdownLocked();

} // namespace

bool ensureInstalled() {
    bool const installed = gInstalled.load(std::memory_order_acquire);
    if (installed && gExecuteHookInstalled.load(std::memory_order_acquire)) {
        return !gShuttingDown.load(std::memory_order_acquire);
    }
    auto const now = GetTickCount64();
    if (now < gInstallRetryAt.load(std::memory_order_acquire)) {
        return installed && !gShuttingDown.load(std::memory_order_acquire);
    }
    std::lock_guard installLock(gInstallMutex);
    if (gInstalled.load(std::memory_order_acquire)) {
        if (gShuttingDown.load(std::memory_order_acquire)) return false;
        if (!gExecuteHookInstalled.load(std::memory_order_acquire)
            && GetTickCount64() >= gInstallRetryAt.load(std::memory_order_acquire)) {
            gInstallRetryAt.store(GetTickCount64() + kInstallRetryIntervalMs, std::memory_order_release);
            // Keep working DXGI/DX11 state; only the missing optional D3D12
            // entry needs another attempt. A partial record stays owned.
            if (tryInstallExecuteHook()) gInstallRetryAt.store(0, std::memory_order_release);
        }
        return true;
    }
    if (GetTickCount64() < gInstallRetryAt.load(std::memory_order_acquire)) return false;

    // Throttle exceptions as well as explicit failures. Never overwrite native
    // target records left by an incomplete rollback or a partial install.
    gInstallRetryAt.store(GetTickCount64() + kInstallRetryIntervalMs, std::memory_order_release);
    if (!detail::prepareOverlayInstall(gShuttingDown, [] { return shutdownLocked(); })) return false;
    // A completed old retirement clears its deadline; the newly admitted
    // attempt must still throttle exceptions before beginning fallible work.
    gInstallRetryAt.store(GetTickCount64() + kInstallRetryIntervalMs, std::memory_order_release);

    auto failInstall = [&]() {
        gInstallRetryAt.store(GetTickCount64() + kInstallRetryIntervalMs, std::memory_order_release);
        if (!shutdownLocked()) {
            logger().error("Overlay installation rollback incomplete; retaining native resources");
        }
        return false;
    };

    if (!companion::beginSession()) return false;
    auto window = findProcessWindow();
    if (!window) return failInstall();
    auto const status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) return failInstall();

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferCount = 1;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = window;
    description.SampleDesc.Count = 1;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    Microsoft::WRL::ComPtr<ID3D11Device> dummyDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> dummyContext;
    Microsoft::WRL::ComPtr<IDXGISwapChain> dummySwapChain;
    if (FAILED(D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &featureLevel, 1, D3D11_SDK_VERSION,
            &description, &dummySwapChain, &dummyDevice, nullptr, &dummyContext
        ))) return failInstall();

    auto** swapVtable = *reinterpret_cast<void***>(dummySwapChain.Get());
    bool ok = installHook(gPresentTarget, swapVtable[kPresentVtableIndex], reinterpret_cast<void*>(presentHook), reinterpret_cast<void**>(&gOriginalPresent))
        && installHook(gResizeTarget, swapVtable[kResizeBuffersVtableIndex], reinterpret_cast<void*>(resizeHook), reinterpret_cast<void**>(&gOriginalResizeBuffers));
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
    if (SUCCEEDED(dummySwapChain.As(&swapChain1))) {
        auto const present1Target = (*reinterpret_cast<void***>(swapChain1.Get()))[kPresent1VtableIndex];
        ok = installHook(gPresent1Target, present1Target, reinterpret_cast<void*>(present1Hook), reinterpret_cast<void**>(&gOriginalPresent1)) && ok;
        swapChain1.Reset();
    }
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain3;
    if (SUCCEEDED(dummySwapChain.As(&swapChain3))) {
        auto const resize1Target = (*reinterpret_cast<void***>(swapChain3.Get()))[kResizeBuffers1VtableIndex];
        ok = installHook(
                 gResize1Target,
                 resize1Target,
                 reinterpret_cast<void*>(resize1Hook),
                 reinterpret_cast<void**>(&gOriginalResizeBuffers1)
             )
          && ok;
        swapChain3.Reset();
    } else {
        ok = false;
    }
    dummySwapChain.Reset();
    dummyContext.Reset();
    dummyDevice.Reset();

    if (!ok) {
        return failInstall();
    }
    bool const executeReady = tryInstallExecuteHook();
    gInstalled.store(true, std::memory_order_release);
    gShuttingDown.store(false, std::memory_order_release);
    if (executeReady) gInstallRetryAt.store(0, std::memory_order_release);
    logger().info("Injected ImGui DXGI hooks installed");
    return true;
}

namespace {

bool shutdownLocked() {
    unsigned allowedCurrentThreadDepth = 0;
    bool const processExitWndProc = insideHookCallback()
        && gHookCallbackKind
        && std::string_view{gHookCallbackKind} == "WndProc"
        && (gHookCallbackMessage == WM_DESTROY || gHookCallbackMessage == WM_NCDESTROY);

    if (insideHookCallback() && !processExitWndProc) {
        logger().error(
            "Overlay teardown requested from inside overlay callback kind={} message=0x{:X}",
            gHookCallbackKind ? gHookCallbackKind : "unknown",
            static_cast<unsigned>(gHookCallbackMessage)
        );
        return false;
    }

    if (processExitWndProc) {
        // LeviLamina disables mods synchronously from Minecraft's WM_DESTROY
        // path. The original WndProc will return into this wrapper after disable
        // completes, so pin LHolo until process exit before dismantling the hooks.
        HMODULE self{};
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&shutdownLocked),
                &self
            )) {
            logger().error("Failed to pin LHolo for WM_DESTROY teardown");
            return false;
        }
        allowedCurrentThreadDepth = gHookCallbackDepth;
        logger().info(
            "Process-exit overlay teardown from WndProc message=0x{:X}; module pinned until process exit",
            static_cast<unsigned>(gHookCallbackMessage)
        );
    }

    gShuttingDown.store(true, std::memory_order_release);
    gMouseHandoffActive.store(false, std::memory_order_release);

    // First stop new detour entries. Do not remove the trampolines or destroy
    // shared D3D/ImGui state until every callback that entered earlier leaves.
    bool hooksDisabled = true;
    hooksDisabled = disableHook(gExecuteTarget) && hooksDisabled;
    hooksDisabled = disableHook(gResize1Target) && hooksDisabled;
    hooksDisabled = disableHook(gPresent1Target) && hooksDisabled;
    hooksDisabled = disableHook(gResizeTarget) && hooksDisabled;
    hooksDisabled = disableHook(gPresentTarget) && hooksDisabled;
    // Complete an initializer that already owns resources. A callback waiting
    // for that mutex will now fail render's under-lock shutdown check instead
    // of installing a WndProc after this snapshot/restoration.
    auto const [window, originalWndProc] = detail::snapshotAfterOverlayInitialization(gResourceMutex, [] {
        return std::pair{gWindow, gOriginalWndProc.load(std::memory_order_acquire)};
    });
    if (originalWndProc && window && IsWindow(window)) {
        auto const currentWndProc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
        if (currentWndProc != windowProc && currentWndProc != originalWndProc) {
            logger().error("Another window subclass still chains through LHolo; retaining DLL until it is detached");
            return false;
        }
        if (currentWndProc == windowProc || gMenuCursorShowCount.load(std::memory_order_acquire) != 0) {
            auto const restored = detail::restoreWindowCursor(window, kMsgRestoreNativeCursor,
                gMenuCursorShowCount, [] { releaseMenuCursor(); });
            if (restored != detail::CursorRestoreResult::Restored) {
                logger().error("Window-thread cursor restoration failed: result={} Win32 error={}; retaining overlay state",
                    static_cast<int>(restored), GetLastError());
                return false;
            }
        }
        SetLastError(0);
        auto const previous = SetWindowLongPtrW(
            window,
            GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(originalWndProc)
        );
        if (previous == 0 && GetLastError() != 0) {
            hooksDisabled = false;
        }
    } else if (gMenuCursorShowCount.load(std::memory_order_acquire) != 0) {
        logger().error("Cursor ownership remains without a reachable game window; retaining overlay state");
        return false;
    }

    if (!hooksDisabled) {
        logger().error("Overlay hook disable failed; retaining LHolo DLL and graphics state");
        return false;
    }

    waitForHookCallbacks(allowedCurrentThreadDepth);
    if (gMenuCursorShowCount.load(std::memory_order_acquire) != 0) {
        logger().error("A drained window callback left cursor ownership outstanding; retaining overlay state");
        return false;
    }

    // Praxis callbacks run inside Present/WndProc and have their own reader
    // barrier. After the outer hook drain, clearing the bridge cannot race UI
    // code executing in the companion DLL.
    if (!companion::shutdown()) {
        logger().error("Companion callback retirement incomplete; retaining LHolo DLL and graphics state");
        return false;
    }

    ClipCursor(nullptr);

    bool hooksRemoved = true;
    hooksRemoved = removeHook(gExecuteTarget) && hooksRemoved;
    hooksRemoved = removeHook(gResize1Target) && hooksRemoved;
    hooksRemoved = removeHook(gPresent1Target) && hooksRemoved;
    hooksRemoved = removeHook(gResizeTarget) && hooksRemoved;
    hooksRemoved = removeHook(gPresentTarget) && hooksRemoved;
    if (!hooksRemoved) return false;
    gOriginalWndProc = nullptr;

    std::lock_guard lock(gResourceMutex);
    std::lock_guard imguiLock(gImGuiMutex);
    releaseGraphicsBackend();
    if (gImGuiInitialized) {
        ImGui_ImplWin32_Shutdown();
        ui::resetFluentTheme();
        ImGui::DestroyContext();
        gImGuiInitialized = false;
    }
    gGameQueue.reset();
    gActiveSwapChain = nullptr;
    {
        std::lock_guard inputLock(gInputStateMutex);
        gGameKeysDown.fill(false);
        gGameMouseButtonsDown.fill(false);
    }
    gConsumeEscapeRelease.store(false, std::memory_order_release);
    gGraphicsResumeAt.store(0, std::memory_order_release);
    gInstallRetryAt.store(0, std::memory_order_release);
    gGuiVisibleLastFrame = false;
    gWindow = nullptr;
    gExecuteHookInstalled.store(false, std::memory_order_release);
    gInstalled.store(false, std::memory_order_release);
    return true;
}

} // namespace

bool shutdown() {
    std::lock_guard installLock(gInstallMutex);
    return shutdownLocked();
}

} // namespace lholo::overlay
