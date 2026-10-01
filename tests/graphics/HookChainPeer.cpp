#include "overlay/NativeHookBinding.h"

namespace {
using Function = int (*)(int);
Function original{};
int increment{};
lholo::overlay::detail::NativeHookBinding binding;
__declspec(noinline) int detour(int value) { return original(value) + increment; }
}

extern "C" __declspec(dllexport) int InstallPeer(void* target, int amount) {
    auto status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) return status;
    void* trampoline{};
    status = binding.create(target, reinterpret_cast<void*>(&detour), &trampoline);
    if (status != MH_OK) return status;
    original = reinterpret_cast<Function>(trampoline);
    increment = amount;
    return binding.enable();
}
extern "C" __declspec(dllexport) bool DisablePeer() { return binding.disable(); }
extern "C" __declspec(dllexport) bool RemovePeer() { return binding.remove(); }
extern "C" __declspec(dllexport) void* PeerHookApi() { return reinterpret_cast<void*>(&MH_CreateHook); }
extern "C" __declspec(dllexport) bool FinishPeer() {
    return !binding.target() && MH_Uninitialize() == MH_OK;
}
