#pragma once

#include <functional>

namespace lholo::app {
// A removed listener no longer receives destruction callbacks from its owner.
// Retire the corresponding borrow before a subsequent attachment can fail.
template <class Owner, class Detach, class Retire>
void detachAndRetireListener(Owner* previous, Detach&& detach, Retire&& retire) {
    if (!previous) return;
    std::invoke(detach, *previous);
    std::invoke(retire);
}
} // namespace lholo::app
