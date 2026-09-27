// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// Process-wide hook lifetime gate. Detours become pass-through as soon as
// shutdown begins, while in-flight callbacks are counted until they return.

#pragma once

namespace lholo::app::hook_lifecycle {

enum class State : unsigned char {
    Disabled,
    Running,
    Quiescing,
};

class DetourGuard final {
public:
    DetourGuard();
    ~DetourGuard();

    DetourGuard(DetourGuard const&) = delete;
    DetourGuard& operator=(DetourGuard const&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return mRunning; }

private:
    bool mCounted{};
    bool mRunning{};
};

[[nodiscard]] bool beginEnable();
void beginQuiesce();
void waitForQuiescence();
void markDisabled();

[[nodiscard]] bool isRunning();
[[nodiscard]] State state();

} // namespace lholo::app::hook_lifecycle
