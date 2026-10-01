#pragma once

#include <functional>
#include <type_traits>

namespace lholo::projection::detail {
// An interrupted scan can already have consumed notifications and changed
// progress. Do not let rendering/placement silently reuse that partial state.
template <class Update, class Failure>
decltype(auto) runCorrectionUpdate(Update&& update, Failure&& failure) {
    static_assert(std::is_nothrow_invocable_v<Failure>);
    try {
        return std::invoke(update);
    } catch (...) {
        std::invoke(failure);
        throw;
    }
}
} // namespace lholo::projection::detail
