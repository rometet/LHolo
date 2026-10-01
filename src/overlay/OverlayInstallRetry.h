#pragma once

#include <atomic>
#include <utility>

namespace lholo::overlay::detail {

template <class FinishRetirement>
bool prepareOverlayInstall(std::atomic_bool& shuttingDown, FinishRetirement&& finishRetirement) {
    // An unsuccessful retirement still owns native hook records. Recreating
    // the same target would return ALREADY_CREATED and erase that ownership.
    if (shuttingDown.load(std::memory_order_acquire)
        && !std::forward<FinishRetirement>(finishRetirement)()) return false;
    // Mark the admitted attempt before any fallible work. An exception must
    // leave callbacks origin-only and require retirement before the next retry.
    shuttingDown.store(true, std::memory_order_release);
    return true;
}

} // namespace lholo::overlay::detail
