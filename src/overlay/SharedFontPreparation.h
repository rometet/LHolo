#pragma once
#include <cstdint>

namespace lholo::overlay::companion::detail {
// Accessed only while the host owns its ImGui/graphics lock. Generation also
// distinguishes re-registration when a DLL/context is allocated at the same address.
class SharedFontPreparation {
    void* mContext{};
    void* mOwner{};
    std::uint64_t mGeneration{};
public:
    bool needsPreparation(void* context, void* owner, std::uint64_t generation) const noexcept {
        return context && owner && (context != mContext || owner != mOwner || generation != mGeneration);
    }
    void prepared(void* context, void* owner, std::uint64_t generation) noexcept {
        mContext = context; mOwner = owner; mGeneration = generation;
    }
    void reset() noexcept { mContext = mOwner = nullptr; mGeneration = 0; }
};
} // namespace lholo::overlay::companion::detail
