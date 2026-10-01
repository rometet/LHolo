#pragma once

#include "overlay/WindowCursorRestore.h"

#include <future>
#include <thread>

namespace lholo::tests::cursor {
inline constexpr UINT kInspect = WM_APP + 1;
inline constexpr UINT kRestore = WM_APP + 2;
inline constexpr UINT kPrepare = WM_APP + 3;
inline constexpr UINT kRestoreSelf = WM_APP + 4;
inline constexpr UINT kHold = WM_APP + 5;
inline constexpr UINT kFinish = WM_APP + 6;

inline int displayCount() {
    auto const after = ShowCursor(TRUE);
    ShowCursor(FALSE);
    return after - 1;
}
inline void setDisplayCount(int target) {
    auto current = displayCount();
    while (current < target) current = ShowCursor(TRUE);
    while (current > target) current = ShowCursor(FALSE);
}

struct WindowState {
    std::atomic_int owned{};
    std::atomic_bool ignoreRestore{};
    std::promise<void> holdEntered, holdRelease;
    std::future<void> holdReleaseFuture{holdRelease.get_future()};
    int initialDisplayCount{};

    void releaseCursor() {
        while (owned.load(std::memory_order_acquire) > 0) {
            owned.fetch_sub(1, std::memory_order_acq_rel);
            ShowCursor(FALSE);
        }
    }
};

inline LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* creation = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
    }
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!state) return DefWindowProcW(window, message, wParam, lParam);
    if (message == kInspect) return displayCount();
    if (message == kRestore) {
        if (!state->ignoreRestore.load(std::memory_order_acquire)) state->releaseCursor();
        return 0;
    }
    if (message == kPrepare) {
        state->owned.store(0, std::memory_order_release);
        setDisplayCount(-1);
        ShowCursor(TRUE);
        state->owned.store(1, std::memory_order_release);
        return 0;
    }
    if (message == kRestoreSelf) {
        return static_cast<LRESULT>(overlay::detail::restoreWindowCursor(window, kRestore, state->owned,
            [&] { state->releaseCursor(); }));
    }
    if (message == kHold) {
        state->holdEntered.set_value();
        state->holdReleaseFuture.wait();
        return 0;
    }
    if (message == kFinish) {
        state->releaseCursor();
        setDisplayCount(state->initialDisplayCount);
        DestroyWindow(window);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

template <class Check>
void runWindowCursorChecks(Check&& check) {
    using overlay::detail::CursorRestoreResult;
    WindowState state;
    std::promise<HWND> ready;
    auto readyFuture = ready.get_future();
    std::thread ui([&] {
        WNDCLASSW klass{};
        klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpfnWndProc = windowProc;
        klass.lpszClassName = L"LHoloAuditCursorChecks";
        if (!RegisterClassW(&klass)) { ready.set_value(nullptr); return; }
        HWND window = CreateWindowW(klass.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
            nullptr, klass.hInstance, &state);
        if (!window) {
            UnregisterClassW(klass.lpszClassName, klass.hInstance);
            ready.set_value(nullptr);
            return;
        }
        state.initialDisplayCount = displayCount();
        ready.set_value(window);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) DispatchMessageW(&message);
        UnregisterClassW(klass.lpszClassName, klass.hInstance);
    });
    HWND window = readyFuture.get();
    if (!check(window != nullptr, "create isolated cursor window thread")) { ui.join(); return; }
    int const callerBefore = displayCount();
    auto prepare = [&] {
        setDisplayCount(callerBefore);
        SendMessageW(window, kPrepare, 0, 0);
    };
    auto restore = [&](HWND target) {
        return overlay::detail::restoreWindowCursor(target, kRestore, state.owned, [&] { state.releaseCursor(); });
    };
    prepare();
    check(restore(window) == CursorRestoreResult::Restored, "shutdown restores cursor through owning window thread");
    check(displayCount() == callerBefore, "cross-thread restoration leaves caller display counter unchanged");
    check(SendMessageW(window, kInspect, 0, 0) == -1, "window cursor counter returns to pre-menu state");
    check(state.owned.load() == 0, "successful delivery clears ownership receipt");
    check(restore(window) == CursorRestoreResult::Restored && SendMessageW(window, kInspect, 0, 0) == -1,
        "repeated restoration is idempotent");

    prepare();
    state.ignoreRestore.store(true, std::memory_order_release);
    check(restore(window) == CursorRestoreResult::ReceiptOutstanding, "consumed private message cannot report cursor restoration");
    check(state.owned.load() == 1, "unhandled restoration retains owned increments for retry");
    state.ignoreRestore.store(false, std::memory_order_release);
    check(restore(window) == CursorRestoreResult::Restored, "delivery can be retried after private message handler resumes");

    prepare();
    check(restore(nullptr) == CursorRestoreResult::WindowUnavailable && state.owned.load() == 1,
        "missing window cannot consume a live ownership receipt on another thread");
    check(SendMessageW(window, kRestoreSelf, 0, 0) == static_cast<LRESULT>(CursorRestoreResult::Restored)
        && SendMessageW(window, kInspect, 0, 0) == -1, "owner-thread teardown restores directly");
    check(restore(nullptr) == CursorRestoreResult::Restored, "empty receipt needs no live window");

    prepare();
    auto held = state.holdEntered.get_future();
    bool const posted = check(PostMessageW(window, kHold, 0, 0) != FALSE, "queue isolated window hold request");
    bool const holdStarted = posted && check(held.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
        "isolated window enters controlled hold");
    if (holdStarted) {
        check(restore(window) == CursorRestoreResult::DeliveryFailed, "blocked window delivery reports bounded failure");
        check(state.owned.load() == 1 && displayCount() == callerBefore, "timed-out delivery preserves both thread counters and receipt");
    }
    state.holdRelease.set_value();
    check(restore(window) == CursorRestoreResult::Restored && SendMessageW(window, kInspect, 0, 0) == -1,
        "late delivery or explicit retry releases exactly the owned increment");
    setDisplayCount(callerBefore);
    SendMessageW(window, kFinish, 0, 0);
    ui.join();
}
} // namespace lholo::tests::cursor
