#pragma once
#include <atomic>
#include <cstdint>

namespace lholo::projection::detail {
// A delayed old callback must neither fail a new session nor erase its fault.
class EpochFailure {
public:
    std::uint64_t token() const noexcept { return mEpoch.load(std::memory_order_acquire); }
    void advance() noexcept { mEpoch.fetch_add(1, std::memory_order_acq_rel); }
    void mark(std::uint64_t epoch) noexcept {
        auto previous = mFailure.load(std::memory_order_acquire);
        while (previous < epoch && !mFailure.compare_exchange_weak(
            previous, epoch, std::memory_order_acq_rel, std::memory_order_acquire)) {}
    }
    bool failed() const noexcept { return mFailure.load(std::memory_order_acquire) == token(); }
private:
    std::atomic_uint64_t mEpoch{1};
    std::atomic_uint64_t mFailure{};
};
} // namespace lholo::projection::detail
