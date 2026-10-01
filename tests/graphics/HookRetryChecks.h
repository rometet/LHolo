#pragma once

#include "overlay/OverlayInstallRetry.h"
#include <MinHook.h>

namespace lholo::tests::hook_retry {
using Function = int (*)(int);
inline Function original{};

__declspec(noinline) inline int target(int value) {
    volatile int retained = value;
    return retained + 1;
}
__declspec(noinline) inline int detour(int value) { return original(value) + 10; }

template <class Check>
void runHookRetryChecks(Check&& check) {
    using overlay::detail::prepareOverlayInstall;
    auto const initialized = MH_Initialize();
    if (!check(initialized == MH_OK, "initialize isolated native MinHook registry")) return;
    void* const knownTarget = reinterpret_cast<void*>(&target);
    void* const knownDetour = reinterpret_cast<void*>(&detour);
    void* trampoline{};
    if (!check(MH_CreateHook(knownTarget, knownDetour, &trampoline) == MH_OK, "create real retry hook")) {
        MH_Uninitialize();
        return;
    }
    original = reinterpret_cast<Function>(trampoline);
    if (!check(MH_EnableHook(knownTarget) == MH_OK, "enable real retry hook")) {
        MH_RemoveHook(knownTarget);
        MH_Uninitialize();
        return;
    }
    check(target(7) == 18, "native hook executes through real original trampoline");
    void* trackedTarget = knownTarget;
    std::atomic_bool shuttingDown{true};
    unsigned cleanupAttempts{};
    int duplicateCreate = -1;
    bool const retryAllowed = prepareOverlayInstall(shuttingDown, [&] {
        ++cleanupAttempts;
        return false; // The first rollback could not disable/remove this hook.
    });
    if (retryAllowed) {
        // The original installer overwrote the tracked field, attempted a
        // duplicate creation, then treated creation failure as unowned.
        trackedTarget = knownTarget;
        duplicateCreate = MH_CreateHook(trackedTarget, knownDetour, &trampoline);
        if (duplicateCreate != MH_OK) trackedTarget = nullptr;
    }
    check(!retryAllowed, "incomplete native retirement blocks a fresh installation attempt");
    check(cleanupAttempts == 1, "retry first attempts completion of prior retirement");
    check(duplicateCreate == -1, "blocked retry cannot recreate an already-owned target");
    check(trackedTarget == knownTarget, "failed retirement keeps real native hook ownership tracked");
    check(target(7) == 18, "blocked retry preserves the existing native hook and trampoline");

    bool const retired = prepareOverlayInstall(shuttingDown, [&] {
        ++cleanupAttempts;
        if (!trackedTarget) return true;
        if (MH_DisableHook(trackedTarget) != MH_OK || MH_RemoveHook(trackedTarget) != MH_OK) return false;
        trackedTarget = nullptr;
        return true;
    });
    check(retired && trackedTarget == nullptr, "completed native retirement admits a new attempt");
    check(cleanupAttempts == 2 && target(7) == 8, "retry removes prior native patch exactly once");
    // Preserve test ownership even in the before-fix run where tracking was lost.
    MH_DisableHook(knownTarget);
    MH_RemoveHook(knownTarget);
    shuttingDown.store(false, std::memory_order_release);
    unsigned unexpectedCleanup{};
    check(prepareOverlayInstall(shuttingDown, [&] { ++unexpectedCleanup; return false; }) && unexpectedCleanup == 0,
        "clean initial installation does not manufacture a retirement");
    check(shuttingDown.load(std::memory_order_acquire), "new native installation remains quiescent until full commit");
    check(MH_CreateHook(knownTarget, knownDetour, &trampoline) == MH_OK, "clean retry recreates native target successfully");
    original = reinterpret_cast<Function>(trampoline);
    check(MH_EnableHook(knownTarget) == MH_OK && target(7) == 18, "new native session uses its own valid trampoline");
    check(!prepareOverlayInstall(shuttingDown, [] { return false; }), "exception during partial installation blocks the next retry");
    check(MH_DisableHook(knownTarget) == MH_OK && MH_RemoveHook(knownTarget) == MH_OK, "new native session retires normally");
    check(target(7) == 8 && MH_Uninitialize() == MH_OK, "native retry fixture leaves no patch or registry behind");
    original = nullptr;
}
} // namespace lholo::tests::hook_retry
