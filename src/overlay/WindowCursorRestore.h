#pragma once

#include <Windows.h>
#include <atomic>
#include <utility>

namespace lholo::overlay::detail {

enum class CursorRestoreResult { Restored, WindowUnavailable, DeliveryFailed, ReceiptOutstanding };

template <class ReleaseCursor>
CursorRestoreResult restoreWindowCursor(
    HWND window, UINT restoreMessage, std::atomic_int const& ownedCount, ReleaseCursor&& releaseCursor
) {
    DWORD processId{};
    auto const threadId = window ? GetWindowThreadProcessId(window, &processId) : 0;
    if (!threadId || processId != GetCurrentProcessId() || !IsWindow(window)) {
        return ownedCount.load(std::memory_order_acquire) == 0
            ? CursorRestoreResult::Restored : CursorRestoreResult::WindowUnavailable;
    }
    if (threadId == GetCurrentThreadId()) {
        std::forward<ReleaseCursor>(releaseCursor)();
    } else {
        // Send even when the receipt is currently zero: an admitted WndProc
        // may still be completing its acquire operation on this window thread.
        DWORD_PTR result{};
        SetLastError(0);
        if (!SendMessageTimeoutW(window, restoreMessage, 0, 0,
            SMTO_BLOCK | SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 1000, &result)) {
            return CursorRestoreResult::DeliveryFailed;
        }
    }
    return ownedCount.load(std::memory_order_acquire) == 0
        ? CursorRestoreResult::Restored : CursorRestoreResult::ReceiptOutstanding;
}

} // namespace lholo::overlay::detail
