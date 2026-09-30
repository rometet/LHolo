// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026 MarmieQi

#include "app/HookLifecycle.h"

#include <atomic>
#include <cstddef>

namespace lholo::app::hook_lifecycle {
namespace {

std::atomic<State>      gState{State::Disabled};
std::atomic_size_t      gActiveDetourThreads{};
thread_local unsigned   gDetourDepth{};
thread_local bool       gOuterRunning{};

void notifyIfIdle(std::size_t previous) noexcept {
    if (previous == 1) gActiveDetourThreads.notify_all();
}

} // namespace

DetourGuard::DetourGuard() noexcept {
    // Nested LHolo detours inherit the outer decision. An already-admitted
    // Running callback is allowed to finish consistently after quiescing starts,
    // while a pass-through callback remains pass-through through nested origins.
    if (gDetourDepth != 0) {
        ++gDetourDepth;
        mRunning = gOuterRunning;
        return;
    }

    // Count every top-level entry, including Quiescing pass-through callbacks.
    // Teardown removes the physical hooks before waiting for this count to reach
    // zero, so origin-only callbacks are visible to the DLL lifetime gate.
    gActiveDetourThreads.fetch_add(1, std::memory_order_acq_rel);
    gDetourDepth = 1;
    mOuterCounted = true;
    gOuterRunning = gState.load(std::memory_order_acquire) == State::Running;
    mRunning = gOuterRunning;
}

DetourGuard::~DetourGuard() {
    if (gDetourDepth != 0) --gDetourDepth;
    if (!mOuterCounted) return;
    gOuterRunning = false;
    notifyIfIdle(gActiveDetourThreads.fetch_sub(1, std::memory_order_acq_rel));
}

bool beginEnable() noexcept {
    // A callback left over from an incomplete teardown must never be allowed to
    // become a newly-admitted Running callback.
    if (gActiveDetourThreads.load(std::memory_order_acquire) != 0) return false;

    auto expected = State::Disabled;
    if (!gState.compare_exchange_strong(
            expected,
            State::Running,
            std::memory_order_acq_rel,
            std::memory_order_acquire
        )) {
        // Never attempt to install native hooks twice. A second enable while
        // Running could make a failed duplicate install roll back live hooks.
        return false;
    }
    return true;
}

void beginQuiesce() noexcept {
    auto expected = State::Running;
    (void)gState.compare_exchange_strong(
        expected,
        State::Quiescing,
        std::memory_order_acq_rel,
        std::memory_order_acquire
    );
}

void waitForQuiescence() {
    // Atomic wait cannot lose the transition-to-zero notification between a
    // predicate check and sleeping, unlike a condition_variable whose producer
    // does not hold the same mutex.
    for (;;) {
        auto const active = gActiveDetourThreads.load(std::memory_order_acquire);
        if (active == 0) return;
        gActiveDetourThreads.wait(active, std::memory_order_acquire);
    }
}

void markDisabled() noexcept {
    if (gActiveDetourThreads.load(std::memory_order_acquire) != 0) return;
    gState.store(State::Disabled, std::memory_order_release);
}

bool isRunning() noexcept {
    return gState.load(std::memory_order_acquire) == State::Running;
}

bool insideDetour() noexcept {
    return gDetourDepth != 0;
}

State state() noexcept {
    return gState.load(std::memory_order_acquire);
}

} // namespace lholo::app::hook_lifecycle
