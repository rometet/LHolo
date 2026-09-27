// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "app/HookLifecycle.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace lholo::app::hook_lifecycle {
namespace {

std::atomic<State>       gState{State::Disabled};
std::atomic_size_t       gActiveRunningDetours{};
std::mutex               gIdleMutex;
std::condition_variable  gIdle;

void notifyIfIdle(std::size_t previous) {
    if (previous == 1) gIdle.notify_all();
}

} // namespace

DetourGuard::DetourGuard() {
    // The hot-path hooks include BlockSource::getBlock, so lifecycle admission
    // must never take a mutex. The two-phase check closes the shutdown race:
    // only callbacks that have confirmed Running remain counted. A callback
    // that loses the race to beginQuiesce() becomes origin-only and removes its
    // provisional count before touching any LHolo-owned state.
    if (gState.load(std::memory_order_acquire) != State::Running) return;

    gActiveRunningDetours.fetch_add(1, std::memory_order_acq_rel);
    if (gState.load(std::memory_order_acquire) != State::Running) {
        notifyIfIdle(gActiveRunningDetours.fetch_sub(1, std::memory_order_acq_rel));
        return;
    }

    mCounted = true;
    mRunning = true;
}

DetourGuard::~DetourGuard() {
    if (!mCounted) return;
    notifyIfIdle(gActiveRunningDetours.fetch_sub(1, std::memory_order_acq_rel));
}

bool beginEnable() {
    auto expected = State::Disabled;
    if (gState.compare_exchange_strong(
            expected,
            State::Running,
            std::memory_order_acq_rel,
            std::memory_order_acquire
        )) {
        return gActiveRunningDetours.load(std::memory_order_acquire) == 0;
    }
    return expected == State::Running;
}

void beginQuiesce() {
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
        return gActiveRunningDetours.load(std::memory_order_acquire) == 0;
    });
}

void markDisabled() {
    if (gActiveRunningDetours.load(std::memory_order_acquire) != 0) return;
    gState.store(State::Disabled, std::memory_order_release);
}

bool isRunning() {
    return gState.load(std::memory_order_acquire) == State::Running;
}

State state() {
    return gState.load(std::memory_order_acquire);
}

} // namespace lholo::app::hook_lifecycle
