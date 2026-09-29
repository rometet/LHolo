[Reading 38 lines from start (total: 38 lines, 0 remaining)]

// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026 MarmieQi

#pragma once

namespace lholo::app::hook_lifecycle {

enum class State : unsigned char {
    Disabled,
    Running,
    Quiescing,
};

class DetourGuard final {
public:
    DetourGuard() noexcept;
    ~DetourGuard();

    DetourGuard(DetourGuard const&) = delete;
    DetourGuard& operator=(DetourGuard const&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return mRunning; }

private:
    bool mOuterCounted{};
    bool mRunning{};
};

[[nodiscard]] bool beginEnable() noexcept;
void beginQuiesce() noexcept;
void waitForQuiescence();
void markDisabled() noexcept;

[[nodiscard]] bool isRunning() noexcept;
[[nodiscard]] bool insideDetour() noexcept;
[[nodiscard]] State state() noexcept;

} // namespace lholo::app::hook_lifecycle

[executed on device: ちひろのPC (a22d5426-96cc-488b-9398-cec6fdb0f382)]