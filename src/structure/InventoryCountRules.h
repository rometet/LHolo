#pragma once
#include <limits>

namespace lholo::structure {
inline int inventoryCountAfterStack(int count, int amount) noexcept {
    if (count < 0) count = 0;
    if (amount <= 0) return count;
    auto const cap = (std::numeric_limits<int>::max)();
    return amount > cap - count ? cap : count + amount;
}
} // namespace lholo::structure
