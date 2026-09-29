[Reading 103 lines from start (total: 103 lines, 0 remaining)]

// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026 MarmieQi

#include "app/HookLifecycle.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace lholo::app::hook_lifecycle {
namespace {

std::atomic<State>      gState{State::Disabled};
std::atomic_size_t      gActiveRunningThreads{};
std::mutex              gIdleMutex;
std::condition_variable gIdle;
thread_local unsigned   gDetourDepth{};

void notifyIfIdle(std::size_t previous) noexcept {
    if (previous == 1) gIdle.notify_all();
}

} // namespace

DetourGuard::DetourGuard() noexcept {
    // Nested LHolo detours on one thread are covered by the outer admission.
    if (gDetourDepth != 0) {
        ++gDetourDepth;
        mRunning = true;
        return;
    }

    if (gState.load(std::memory_order_acquire) != State::Running) return;

    gActiveRunningThreads.fetch_add(1, std::memory_order_acq_rel);
    if (gState.load(std::memory_order_acquire) != State::Running) {
        notifyIfIdle(gActiveRunningThreads.fetch_sub(1, std::memory_order_acq_rel));
        return;
    }

    gDetourDepth = 1;
    mOuterCounted = true;
    mRunning = true;
}

DetourGuard::~DetourGuard() {
    if (!mRunning) return;
    if (gDetourDepth != 0) --gDetourDepth;
    if (!mOuterCounted) return;
    notifyIfIdle(gActiveRunningThreads.fetch_sub(1, std::memory_order_acq_rel));
}

bool beginEnable() noexcept {
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
    return gActiveRunningThreads.load(std::memory_order_acquire) == 0;
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
    std::unique_lock lock(gIdleMutex);
    gIdle.wait(lock, [] {
        return gActiveRunningThreads.load(std::memory_order_acquire) == 0;
    });
}

void markDisabled() noexcept {
    if (gActiveRunningThreads.load(std::memory_order_acquire) != 0) return;
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

[executed on device: ちひろのPC (a22d5426-96cc-488b-9398-cec6fdb0f382)]