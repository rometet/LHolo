#pragma once

#include <atomic>
#include <functional>
#include <type_traits>
#include <utility>
#include "app/HookLifecycle.h"

namespace lholo::projection::detail {

// A task or completion publication can allocate/throw. Busy must be released
// on either failure; report a fatal task to the frame owner without allocating
// another synthetic result whose missing identity would leave a section stuck.
template <class Task, class Publish, class Failure>
void runWorkerTaskBoundary(std::atomic_bool& busy, Task&& task, Publish&& publish, Failure&& failure) noexcept {
    static_assert(std::is_nothrow_invocable_v<Failure>);
    struct ResetBusy {
        std::atomic_bool& value;
        ~ResetBusy() { value.store(false, std::memory_order_release); }
    } reset{busy};
    try {
        std::invoke(std::forward<Publish>(publish), std::invoke(std::forward<Task>(task)));
    } catch (...) {
        std::invoke(std::forward<Failure>(failure));
    }
}

// Every task's native work and captured-owner destruction belong to one lease.
// Move the callable out of the executor closure so its captures are destroyed
// before the lease ends, rather than later when the executor clears that closure.
template <class Task, class Publish, class Failure>
void runLeasedWorkerTaskBoundary(std::atomic_bool& busy, Task& task, Publish&& publish, Failure&& failure) noexcept {
    static_assert(std::is_nothrow_move_constructible_v<Task>);
    static_assert(std::is_nothrow_default_constructible_v<Task>);
    static_assert(std::is_nothrow_move_assignable_v<Task>);
    app::hook_lifecycle::DetourGuard taskLease;
    auto ownedTask = std::move(task);
    // A moved-from callable has a valid but unspecified state. Explicitly
    // empty it while the lease is held so no executor capture survives it.
    task = Task{};
    if (!taskLease) {
        // The owner joins this canceled task before removing native hooks.
        busy.store(false, std::memory_order_release);
        return;
    }
    runWorkerTaskBoundary(busy, ownedTask, std::forward<Publish>(publish), std::forward<Failure>(failure));
}

} // namespace lholo::projection::detail
