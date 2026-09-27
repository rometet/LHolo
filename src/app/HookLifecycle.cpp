// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi

#include "app/HookLifecycle.h"

#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace lholo::app::hook_lifecycle {
namespace {

std::mutex              gMutex;
std::condition_variable gIdle;
State                   gState{State::Disabled};
std::size_t             gActiveDetours{};

} // namespace

DetourGuard::DetourGuard() {
    std::lock_guard lock(gMutex);
    if (gState == State::Disabled) return;
    ++gActiveDetours;
    mCounted = true;
    mRunning = gState == State::Running;
}

DetourGuard::~DetourGuard() {
    if (!mCounted) return;
    std::lock_guard lock(gMutex);
    if (gActiveDetours != 0) --gActiveDetours;
    if (gActiveDetours == 0) gIdle.notify_all();
}

bool beginEnable() {
    std::lock_guard lock(gMutex);
    if (gState == State::Running) return true;
    if (gState != State::Disabled || gActiveDetours != 0) return false;
    gState = State::Running;
    return true;
}

void beginQuiesce() {
    std::lock_guard lock(gMutex);
    if (gState == State::Running) gState = State::Quiescing;
}

void waitForQuiescence() {
    std::unique_lock lock(gMutex);
    gIdle.wait(lock, [] { return gActiveDetours == 0; });
}

void markDisabled() {
    std::lock_guard lock(gMutex);
    if (gActiveDetours == 0) gState = State::Disabled;
}

bool isRunning() {
    std::lock_guard lock(gMutex);
    return gState == State::Running;
}

State state() {
    std::lock_guard lock(gMutex);
    return gState;
}

} // namespace lholo::app::hook_lifecycle
