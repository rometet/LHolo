// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026 MarmieQi

#include "app/HookLifecycle.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace lholo::app::hook_lifecycle {
namespace {

constexpr unsigned kStateShift = 62;
constexpr std::uint64_t kRunningCountMask = (std::uint64_t{1} << kStateShift) - 1;
constexpr std::uint64_t encodedState(State state) noexcept {
    return static_cast<std::uint64_t>(state) << kStateShift;
}
constexpr State decodedState(std::uint64_t value) noexcept {
    return static_cast<State>(value >> kStateShift);
}
// State and admitted-body count share one atomic modification order. Closing
// Running admission cannot race a count increment into a false zero snapshot.
std::atomic_uint64_t    gRunningAdmission{encodedState(State::Disabled)};
std::atomic_size_t      gActiveDetourThreads{};
thread_local unsigned   gDetourDepth{};
thread_local bool       gOuterRunning{};

void notifyIfIdle(std::size_t previous) noexcept {
    if (previous == 1) gActiveDetourThreads.notify_all();
}

bool admitRunningBody() noexcept {
    auto current = gRunningAdmission.load(std::memory_order_acquire);
    while (decodedState(current) == State::Running) {
        if ((current & kRunningCountMask) == kRunningCountMask) return false;
        if (gRunningAdmission.compare_exchange_weak(current, current + 1,
                std::memory_order_acq_rel, std::memory_order_acquire)) return true;
    }
    return false;
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
    gOuterRunning = admitRunningBody();
    mRunning = gOuterRunning;
}

DetourGuard::~DetourGuard() {
    if (gDetourDepth != 0) --gDetourDepth;
    if (!mOuterCounted) return;
    if (mRunning) {
        auto const previous = gRunningAdmission.fetch_sub(1, std::memory_order_acq_rel);
        if ((previous & kRunningCountMask) == 1) gRunningAdmission.notify_all();
    }
    gOuterRunning = false;
    notifyIfIdle(gActiveDetourThreads.fetch_sub(1, std::memory_order_acq_rel));
}

bool beginEnable() noexcept {
    // A callback left over from an incomplete teardown must never be allowed to
    // become a newly-admitted Running callback.
    if (gActiveDetourThreads.load(std::memory_order_acquire) != 0) return false;

    auto expected = encodedState(State::Disabled);
    if (!gRunningAdmission.compare_exchange_strong(
            expected,
            encodedState(State::Running),
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
    auto current = gRunningAdmission.load(std::memory_order_acquire);
    while (decodedState(current) == State::Running) {
        auto const closed = encodedState(State::Quiescing) | (current & kRunningCountMask);
        if (gRunningAdmission.compare_exchange_weak(current, closed,
                std::memory_order_acq_rel, std::memory_order_acquire)) return;
    }
}

void waitForRunningCallbacks() {
    for (;;) {
        auto const admission = gRunningAdmission.load(std::memory_order_acquire);
        if ((admission & kRunningCountMask) == 0) return;
        gRunningAdmission.wait(admission, std::memory_order_acquire);
    }
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
    auto current = gRunningAdmission.load(std::memory_order_acquire);
    if ((current & kRunningCountMask) != 0) return;
    (void)gRunningAdmission.compare_exchange_strong(current, encodedState(State::Disabled),
        std::memory_order_acq_rel, std::memory_order_acquire);
}

bool isRunning() noexcept {
    return decodedState(gRunningAdmission.load(std::memory_order_acquire)) == State::Running;
}

bool insideDetour() noexcept {
    return gDetourDepth != 0;
}

State state() noexcept {
    return decodedState(gRunningAdmission.load(std::memory_order_acquire));
}

} // namespace lholo::app::hook_lifecycle
