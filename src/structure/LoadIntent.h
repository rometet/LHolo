#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>

namespace lholo::structure::detail {

// Parse futures belong to the Present control plane. Other threads invalidate
// the intent, never inspect or mutate those futures. Publishing a result and
// withdrawing a structure are ordered by this small commit gate.
class LoadIntent {
public:
    std::uint64_t begin() {
        return invalidateAndApply([] {});
    }

    bool current(std::uint64_t ticket) const {
        std::lock_guard lock(mMutex);
        return ticket == mTicket;
    }

    template <class Operation>
    bool applyIfCurrent(std::uint64_t ticket, Operation&& operation) {
        std::lock_guard lock(mMutex);
        if (ticket != mTicket) return false;
        std::invoke(std::forward<Operation>(operation));
        return true;
    }

    template <class Operation>
    std::uint64_t invalidateAndApply(Operation&& operation) {
        std::lock_guard lock(mMutex);
        ++mTicket;
        std::invoke(std::forward<Operation>(operation));
        return mTicket;
    }

private:
    mutable std::mutex mMutex;
    std::uint64_t mTicket{};
};

} // namespace lholo::structure::detail
