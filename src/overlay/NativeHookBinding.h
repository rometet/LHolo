#pragma once

#include <Windows.h>
#include <MinHook.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <utility>

namespace lholo::overlay::detail {

static_assert(sizeof(std::uintptr_t) == 8, "Native overlay hook ownership requires Windows x64");

inline bool readHookBytes(std::uintptr_t address, void* bytes, std::size_t size) noexcept {
    SIZE_T received{};
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void const*>(address), bytes, size, &received)
        && received == size;
}

// MinHook 1.3.4 x64 emits E9 -> FF25 [RIP+0] -> absolute detour. Its
// patch-above form uses EB F9 at the entry and E9 in the preceding five bytes.
// Never dereference code/relay memory directly: another owner may have retired it.
inline bool minHookPatchTargets(void* target, void* detour) noexcept {
    if (!target || !detour) return false;
    auto patch = reinterpret_cast<std::uintptr_t>(target);
    std::array<unsigned char, 2> entry{};
    if (!readHookBytes(patch, entry.data(), entry.size())) return false;
    if (entry[0] == 0xEB && entry[1] == 0xF9) {
        if (patch < 5) return false;
        patch -= 5;
    }
    std::array<unsigned char, 5> jump{};
    if (!readHookBytes(patch, jump.data(), jump.size()) || jump[0] != 0xE9) return false;
    std::int32_t displacement{};
    std::memcpy(&displacement, jump.data() + 1, sizeof(displacement));
    auto const relay = patch + jump.size() + static_cast<std::uintptr_t>(static_cast<std::intptr_t>(displacement));
    std::array<unsigned char, 14> absolute{};
    if (!readHookBytes(relay, absolute.data(), absolute.size()) || absolute[0] != 0xFF || absolute[1] != 0x25) return false;
    std::uint32_t indirectOffset{};
    std::memcpy(&indirectOffset, absolute.data() + 2, sizeof(indirectOffset));
    std::uintptr_t destination{};
    std::memcpy(&destination, absolute.data() + 6, sizeof(destination));
    return indirectOffset == 0 && destination == reinterpret_cast<std::uintptr_t>(detour);
}

// External synchronization owns this record. Destruction deliberately does not
// unhook: callers must disable, drain callbacks, then remove the trampoline.
class NativeHookBinding {
    void* mTarget{};
    void* mDetour{};
    bool mEnabled{};
    bool mEverEnabled{};
    std::array<unsigned char, 5> mEntryBeforeCreate{};
    MH_STATUS mLastStatus{MH_OK};

public:
    NativeHookBinding() = default;
    NativeHookBinding(NativeHookBinding const&) = delete;
    NativeHookBinding& operator=(NativeHookBinding const&) = delete;
    void* target() const noexcept { return mTarget; }
    bool enabled() const noexcept { return mEnabled; }
    // Includes readable-entry/ownership refusals as well as native API status.
    MH_STATUS lastStatus() const noexcept { return mLastStatus; }
    bool currentPatchOwned() const noexcept { return minHookPatchTargets(mTarget, mDetour); }

    MH_STATUS create(void* target, void* detour, void** original) noexcept {
        if (mTarget) return mLastStatus = MH_ERROR_ALREADY_CREATED;
        std::array<unsigned char, 5> entry{};
        if (!target || !readHookBytes(reinterpret_cast<std::uintptr_t>(target), entry.data(), entry.size())) {
            return mLastStatus = MH_ERROR_NOT_EXECUTABLE;
        }
        mLastStatus = MH_CreateHook(target, detour, original);
        if (mLastStatus == MH_OK) {
            mTarget = target;
            mDetour = detour;
            mEntryBeforeCreate = entry;
        }
        return mLastStatus;
    }

    MH_STATUS enable() noexcept {
        if (!mTarget) return mLastStatus = MH_ERROR_NOT_CREATED;
        if (mEnabled) return mLastStatus = MH_ERROR_ENABLED;
        std::array<unsigned char, 5> entry{};
        if (!readHookBytes(reinterpret_cast<std::uintptr_t>(mTarget), entry.data(), entry.size())) {
            return mLastStatus = MH_ERROR_MEMORY_PROTECT;
        }
        // A failed enable can be retried later. Preserve a peer that changed
        // the entry after CreateHook; this old trampoline did not capture it.
        if (entry != mEntryBeforeCreate) return mLastStatus = MH_ERROR_UNSUPPORTED_FUNCTION;
        mLastStatus = MH_EnableHook(mTarget);
        if (mLastStatus == MH_OK || mLastStatus == MH_ERROR_ENABLED) mEnabled = mEverEnabled = true;
        return mLastStatus;
    }

    template <class DiscoverTarget>
    bool installOrRetry(void* detour, void** original, DiscoverTarget&& discoverTarget) {
        if (mTarget && !mEnabled && !mEverEnabled) {
            std::array<unsigned char, 5> entry{};
            if (readHookBytes(reinterpret_cast<std::uintptr_t>(mTarget), entry.data(), entry.size())
                && entry != mEntryBeforeCreate) {
                // This record never admitted our callback, so no caller can
                // borrow its trampoline. Recreate against the new peer chain.
                if (!remove()) return false;
            }
        }
        if (!mTarget) {
            auto* const target = std::forward<DiscoverTarget>(discoverTarget)();
            if (!target || create(target, detour, original) != MH_OK) return false;
        }
        // An enable failure still owns its original record. Retry that record
        // rather than discovering/recreating a target and losing ownership.
        if (mEnabled) return true;
        auto const status = enable();
        return status == MH_OK || status == MH_ERROR_ENABLED;
    }

    bool disable() noexcept {
        if (!mTarget || !mEnabled) return true;
        // A later hook's trampoline may still call our relay. Restoring our
        // backup would remove that hook and let it later restore freed code.
        if (!currentPatchOwned()) return false;
        mLastStatus = MH_DisableHook(mTarget);
        if (mLastStatus != MH_OK && mLastStatus != MH_ERROR_DISABLED && mLastStatus != MH_ERROR_NOT_CREATED) return false;
        mEnabled = false;
        return true;
    }

    bool remove() noexcept {
        if (!mTarget) return true;
        if (mEnabled) return false; // MH_RemoveHook would otherwise implicitly disable.
        mLastStatus = MH_RemoveHook(mTarget);
        if (mLastStatus != MH_OK && mLastStatus != MH_ERROR_NOT_CREATED) return false;
        mTarget = mDetour = nullptr;
        mEverEnabled = false;
        return true;
    }
};

} // namespace lholo::overlay::detail
