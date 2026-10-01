#pragma once

#include <atomic>
#include <mutex>
#include <utility>

namespace lholo::overlay::detail {

inline std::unique_lock<std::mutex> acquireLiveOverlayResources(
    std::mutex& resources, std::atomic_bool const& shuttingDown
) {
    std::unique_lock lock(resources);
    // A Present may have passed its fast check before waiting behind another
    // renderer/resize. It must not install a new WndProc after teardown starts.
    if (shuttingDown.load(std::memory_order_acquire)) lock.unlock();
    return lock;
}

template <class ReadSnapshot>
auto snapshotAfterOverlayInitialization(std::mutex& resources, ReadSnapshot&& readSnapshot) {
    // Finish an already-admitted initializer before inspecting its window.
    // Release this lock before any cross-thread Win32 operation or callback drain.
    std::lock_guard lock(resources);
    return std::forward<ReadSnapshot>(readSnapshot)();
}

} // namespace lholo::overlay::detail
