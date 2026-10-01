#pragma once

#include "overlay/NativeHookBinding.h"
#include <stdexcept>

namespace lholo::tests::deferred_hook {
using Function = int (*)(int);
inline Function original{};
__declspec(noinline) inline int target(int value) { volatile int retained = value; return retained + 1; }
__declspec(noinline) inline int detour(int value) { return original(value) + 10; }

template <class Check>
bool runDeferredHookChecks(Check&& check) {
    using overlay::detail::NativeHookBinding;
    if (!check(MH_Initialize() == MH_OK, "initialize deferred native hook registry")) return false;
    NativeHookBinding binding;
    void* trampoline{};
    auto const knownDetour = reinterpret_cast<void*>(&detour);
    unsigned discoveries{};
    check(!binding.installOrRetry(knownDetour, &trampoline, [&]() -> void* { ++discoveries; return nullptr; }),
        "temporarily unavailable target defers native installation");
    check(!binding.target() && !binding.enabled() && target(7) == 8, "discovery failure preserves native function");
    auto const ready = binding.installOrRetry(knownDetour, &trampoline, [&]() -> void* { ++discoveries; return reinterpret_cast<void*>(&target); });
    check(ready && discoveries == 2 && binding.target(), "later target availability installs through actual MinHook");
    if (!ready) return false;
    original = reinterpret_cast<Function>(trampoline);
    check(target(7) == 18, "deferred installation recovers the native callback");
    check(binding.installOrRetry(knownDetour, &trampoline, [&]() -> void* { ++discoveries; return nullptr; })
        && discoveries == 2 && target(7) == 18, "committed native hook skips repeated target discovery");
    if (!check(binding.disable() && binding.remove(), "deferred native session retires normally")) return false;
    bool exceptionObserved{};
    try {
        binding.installOrRetry(knownDetour, &trampoline, []() -> void* { throw std::runtime_error("discovery failed"); });
    } catch (std::runtime_error const&) { exceptionObserved = true; }
    check(exceptionObserved && !binding.target() && target(7) == 8, "discovery exception leaves no native ownership behind");
    check(!binding.installOrRetry(knownDetour, &trampoline, [] { return reinterpret_cast<void*>(1); })
        && binding.lastStatus() == MH_ERROR_NOT_EXECUTABLE && !binding.target(), "inaccessible target stays retryable without unsafe code reads");

    // Free only our own executable page between CreateHook and EnableHook to
    // trigger a real MH_ERROR_MEMORY_PROTECT, then restore the same native target.
    auto* const page = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!check(page != nullptr, "allocate native enable-failure fixture")) return false;
    unsigned char const body[]{0x8B, 0xC1, 0x83, 0xC0, 0x01, 0xC3};
    // Leave readable prefix padding for instrumented indirect-call metadata
    // probes, as for the patch-above native fixture in HookChainChecks.
    auto* const targetAddress = page + 16;
    std::memcpy(targetAddress, body, sizeof(body));
    check(!binding.installOrRetry(knownDetour, &trampoline, [&]() -> void* { return targetAddress; })
        && binding.lastStatus() == MH_ERROR_NOT_EXECUTABLE && !binding.target(), "actual MinHook rejects readable but non-executable discovery");
    DWORD previous{};
    if (!check(VirtualProtect(page, 4096, PAGE_EXECUTE_READ, &previous) != FALSE, "prepare native enable-failure target")) return false;
    FlushInstructionCache(GetCurrentProcess(), page, 4096);
    if (!check(binding.create(targetAddress, knownDetour, &trampoline) == MH_OK, "create real hook before native protection failure")) return false;
    original = reinterpret_cast<Function>(trampoline);
    if (!check(VirtualFree(page, 0, MEM_RELEASE) != FALSE, "temporarily retire only this fixture's target page")) return false;
    check(MH_EnableHook(targetAddress) == MH_ERROR_MEMORY_PROTECT, "actual MinHook reports target-page protection failure");
    unsigned rediscoveries{};
    check(!binding.installOrRetry(knownDetour, &trampoline, [&]() -> void* { ++rediscoveries; return nullptr; })
        && binding.lastStatus() == MH_ERROR_MEMORY_PROTECT, "real enable failure remains eligible for retry");
    check(binding.target() == targetAddress && !binding.enabled() && rediscoveries == 0, "enable failure retains original record without rediscovery");
    auto* const restored = static_cast<unsigned char*>(VirtualAlloc(page, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!check(restored == page, "restore exact native target address")) return false;
    std::memcpy(restored + 16, body, sizeof(body));
    if (!check(VirtualProtect(restored, 4096, PAGE_EXECUTE_READ, &previous) != FALSE, "restore native target protection")) return false;
    FlushInstructionCache(GetCurrentProcess(), restored, 4096);
    auto const enabled = binding.installOrRetry(knownDetour, &trampoline, [&]() -> void* { ++rediscoveries; return nullptr; });
    check(enabled && rediscoveries == 0 && binding.target() == restored + 16, "retry enables the existing record instead of duplicating CreateHook");
    if (!enabled) return false;
    check(reinterpret_cast<Function>(restored + 16)(7) == 18, "recovered native target executes its retained original trampoline");
    if (!check(binding.disable() && binding.remove(), "recovered enable-failure hook retires fully")) return false;
    check(reinterpret_cast<Function>(restored + 16)(7) == 8, "retirement restores recovered target's native implementation");
    check(VirtualFree(restored, 0, MEM_RELEASE) != FALSE && MH_Uninitialize() == MH_OK, "deferred hook fixture frees page and native registry");
    original = nullptr;
    return true;
}
} // namespace lholo::tests::deferred_hook
