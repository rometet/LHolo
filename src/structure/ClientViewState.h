#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace lholo::structure::detail {

struct ClientViewSnapshot {
    float yaw{};
    std::array<float, 3> forward{};
    std::uint64_t worldEpoch{};
};

// Protected by the capture value-state mutex. Identity pointers are compared
// only; input and Present receive copied values, never an engine borrow.
class ClientViewState {
public:
    void publish(void const* level, void const* dimension, float yaw, std::array<float, 3> forward) {
        if (level != mLevel || dimension != mDimension) ++mEpoch;
        mLevel = level;
        mDimension = dimension;
        mSnapshot = ClientViewSnapshot{yaw, forward, mEpoch};
    }
    void invalidate() noexcept {
        mSnapshot.reset();
        mLevel = mDimension = nullptr;
        ++mEpoch;
    }
    std::optional<ClientViewSnapshot> snapshot() const noexcept { return mSnapshot; }
private:
    void const* mLevel{};
    void const* mDimension{};
    std::uint64_t mEpoch{};
    std::optional<ClientViewSnapshot> mSnapshot;
};

} // namespace lholo::structure::detail
