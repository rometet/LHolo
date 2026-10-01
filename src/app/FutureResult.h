#pragma once

#include <future>
#include <optional>
#include <type_traits>

namespace lholo::app {
template <class T>
T takePendingValue(std::optional<T>& inFlight) {
    static_assert(std::is_nothrow_move_constructible_v<T>);
    auto pending = std::move(*inFlight);
    inFlight.reset();
    return pending;
}

template <class T>
T takeFutureResult(std::optional<std::future<T>>& inFlight) {
    // get() consumes a future even when the task throws. Retire the owner slot
    // first so the next request never calls get() on a future with no state.
    auto future = takePendingValue(inFlight);
    return future.get();
}
} // namespace lholo::app
