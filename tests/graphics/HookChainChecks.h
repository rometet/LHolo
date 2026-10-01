#pragma once

#include "overlay/NativeHookBinding.h"
#include <array>
#include <string>

namespace lholo::tests::hook_chain {
using Function = int (*)(int);
inline Function original{};
__declspec(noinline) inline int target(int value) { volatile int retained = value; return retained + 1; }
__declspec(noinline) inline int detour(int value) { return original(value) + 10; }

template <class Check>
bool runHookChainChecks(Check&& check) {
    using overlay::detail::NativeHookBinding;
    using overlay::detail::minHookPatchTargets;
    std::array<wchar_t, 32768> modulePath{};
    auto const length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if (!check(length != 0 && length < modulePath.size(), "locate native hook-chain fixture executable")) return false;
    std::wstring peerPath{modulePath.data(), length};
    auto const separator = peerPath.find_last_of(L"\\/");
    if (!check(separator != std::wstring::npos, "native peer DLL has an absolute local directory")) return false;
    peerPath.resize(separator + 1);
    peerPath += L"LHoloHookChainPeer.dll";
    auto const peer = LoadLibraryW(peerPath.c_str());
    if (!check(peer != nullptr, "load independent static-MinHook peer DLL")) return false;
    auto const installPeer = reinterpret_cast<int (*)(void*, int)>(GetProcAddress(peer, "InstallPeer"));
    auto const disablePeer = reinterpret_cast<bool (*)()>(GetProcAddress(peer, "DisablePeer"));
    auto const removePeer = reinterpret_cast<bool (*)()>(GetProcAddress(peer, "RemovePeer"));
    auto const peerApi = reinterpret_cast<void* (*)()>(GetProcAddress(peer, "PeerHookApi"));
    auto const finishPeer = reinterpret_cast<bool (*)()>(GetProcAddress(peer, "FinishPeer"));
    if (!check(installPeer && disablePeer && removePeer && peerApi && finishPeer, "peer exposes complete native fixture API")) {
        FreeLibrary(peer);
        return false;
    }
    check(peerApi() != reinterpret_cast<void*>(&MH_CreateHook), "two DLLs use independent native MinHook registries");
    if (!check(MH_Initialize() == MH_OK, "initialize primary hook-chain registry")) {
        FreeLibrary(peer);
        return false;
    }
    NativeHookBinding own;
    auto const knownTarget = reinterpret_cast<void*>(&target);
    auto const knownDetour = reinterpret_cast<void*>(&detour);
    auto createOwn = [&] {
        void* trampoline{};
        auto const status = own.create(knownTarget, knownDetour, &trampoline);
        if (!check(status == MH_OK, "create primary chain hook")) return false;
        original = reinterpret_cast<Function>(trampoline);
        return check(own.enable() == MH_OK, "enable primary chain hook");
    };
    if (!createOwn() || !check(target(7) == 18 && own.currentPatchOwned(), "first owner publishes its actual relay")) return false;
    void* duplicateOriginal{};
    check(own.create(knownTarget, knownDetour, &duplicateOriginal) == MH_ERROR_ALREADY_CREATED
        && own.target() == knownTarget && own.enabled(), "duplicate attempt cannot overwrite an existing ownership record");
    if (!check(installPeer(knownTarget, 20) == MH_OK, "second independent owner hooks the same target")) return false;
    check(target(7) == 38 && !own.currentPatchOwned(), "second owner chains through first while owning the current patch");
    auto const disabledBelowPeer = own.disable();
    check(!disabledBelowPeer, "lower hook cannot disable a later owner's current patch");
    check(!own.remove(), "enabled lower hook cannot free its relay through implicit removal");
    if (!check(!disabledBelowPeer && own.enabled() && own.target() == knownTarget,
        "deferred teardown keeps native ownership and trampoline alive")) return false;
    check(target(7) == 38, "deferred lower teardown preserves both real callbacks");
    if (!check(disablePeer() && removePeer(), "upper peer detaches before lower owner retries")) return false;
    check(target(7) == 18 && own.currentPatchOwned(), "peer removal restores the still-live lower relay");
    if (!check(own.disable(), "lower owner can disable after peer departure")) return false;
    check(target(7) == 8 && !own.enabled(), "disabling lower owner restores native function");
    check(own.remove() && own.remove() && own.disable(), "disabled removal and repeated teardown are idempotent");

    // Reverse load order: the first peer must defer while LHolo is above it.
    if (!check(installPeer(knownTarget, 20) == MH_OK, "peer can install first in a new session") || !createOwn()) return false;
    check(target(7) == 38 && own.currentPatchOwned(), "LHolo installed second preserves the first callback");
    check(!disablePeer() && !removePeer() && target(7) == 38, "first peer retains its relay while a later owner remains");
    if (!check(own.disable() && own.remove(), "later LHolo owner retires first")) return false;
    check(target(7) == 28, "LHolo departure preserves the first peer callback");
    if (!check(disablePeer() && removePeer(), "first peer retires after LHolo departure")) return false;
    check(target(7) == 8, "both load orders return the target to its native implementation");

    // A retained, not-yet-enabled record must not overwrite a peer installed
    // between CreateHook and an eventual EnableHook retry.
    void* pendingTrampoline{};
    if (!check(own.create(knownTarget, knownDetour, &pendingTrampoline) == MH_OK, "create pending native record before another owner arrives")) return false;
    original = reinterpret_cast<Function>(pendingTrampoline);
    if (!check(installPeer(knownTarget, 20) == MH_OK, "peer installs above an unenabled record")) return false;
    auto const pendingEnable = own.enable();
    check(pendingEnable != MH_OK && !own.enabled(), "pending enable refuses changed native entry bytes");
    check(target(7) == 28, "refused pending enable preserves the later peer's callback");
    if (pendingEnable == MH_OK) {
        // Preserve test safety in the old blind-enable policy run.
        own.disable();
    }
    if (!check(own.remove() && disablePeer() && removePeer(), "pending record and peer retire without overwriting each other")) return false;
    check(target(7) == 8, "pending native ownership scenario restores original function");

    if (!check(own.create(knownTarget, knownDetour, &pendingTrampoline) == MH_OK
        && installPeer(knownTarget, 20) == MH_OK, "deferred discovery encounters a newly installed peer")) return false;
    bool const rebased = own.installOrRetry(knownDetour, &pendingTrampoline, [&] { return knownTarget; });
    original = reinterpret_cast<Function>(pendingTrampoline);
    check(rebased && target(7) == 38, "never-enabled record safely recaptures the new peer chain before commit");
    if (!check(own.disable() && own.remove(), "rebased later owner retires above peer")) return false;
    check(target(7) == 28 && disablePeer() && removePeer(), "rebased retirement preserves peer then permits peer departure");

    if (!createOwn() || !check(own.disable() && installPeer(knownTarget, 20) == MH_OK,
        "previously enabled record is disabled before peer arrival")) return false;
    auto const borrowedOriginal = original;
    pendingTrampoline = reinterpret_cast<void*>(borrowedOriginal);
    unsigned forbiddenDiscoveries{};
    bool const borrowedRetry = own.installOrRetry(knownDetour, &pendingTrampoline, [&] {
        ++forbiddenDiscoveries;
        return knownTarget;
    });
    check(!borrowedRetry && forbiddenDiscoveries == 0 && !own.enabled(), "previously enabled record is never rebased before its callback drain");
    check(!borrowedRetry && borrowedOriginal(7) == 8 && pendingTrampoline == reinterpret_cast<void*>(borrowedOriginal),
        "deferred retry keeps an existing original-trampoline borrow usable");
    if (borrowedRetry) own.disable(); // Safe cleanup in the before-policy run.
    if (!check(own.remove() && disablePeer() && removePeer(), "after fixture callback drain both retained records retire normally")) return false;

    // Real short native function forces MinHook's patch-above form. Its bytes
    // are x64 MOV EAX,ECX; RET, followed by non-padding unreachable bytes.
    auto* const page = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!check(page != nullptr, "allocate isolated native hotpatch fixture")) return false;
    std::memset(page, 0xCC, 4096);
    unsigned char const body[]{0x8B, 0xC1, 0xC3, 0x41, 0x42};
    std::memcpy(page + 16, body, sizeof(body));
    DWORD previous{};
    if (!check(VirtualProtect(page, 4096, PAGE_EXECUTE_READ, &previous) != FALSE, "make fixture executable")) {
        VirtualFree(page, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), page, 4096);
    auto const hotTarget = page + 16;
    auto const hotFunction = reinterpret_cast<Function>(hotTarget);
    void* trampoline{};
    if (!check(own.create(hotTarget, knownDetour, &trampoline) == MH_OK, "create real patch-above hook")) return false;
    original = reinterpret_cast<Function>(trampoline);
    if (!check(own.enable() == MH_OK, "enable real patch-above hook")) return false;
    check(hotTarget[0] == 0xEB && hotTarget[1] == 0xF9 && own.currentPatchOwned(), "short entry identifies its own MinHook relay");
    check(hotFunction(7) == 17, "patch-above fixture calls actual detour and original");
    check(!minHookPatchTargets(nullptr, knownDetour) && !minHookPatchTargets(reinterpret_cast<void*>(1), knownDetour),
        "invalid or inaccessible code is rejected without dereferencing");
    if (!check(own.disable() && own.remove(), "patch-above hook restores entry and preceding padding")) return false;
    check(hotFunction(7) == 7 && std::memcmp(hotTarget, body, sizeof(body)) == 0, "native hotpatch bytes restored exactly");
    VirtualFree(page, 0, MEM_RELEASE);
    check(!minHookPatchTargets(hotTarget, knownDetour), "retired code region can be inspected without access violation");
    check(MH_Uninitialize() == MH_OK && finishPeer(), "both independent native registries retire completely");
    check(FreeLibrary(peer) != FALSE, "fully retired peer DLL unloads safely");
    original = nullptr;
    return true;
}
} // namespace lholo::tests::hook_chain
