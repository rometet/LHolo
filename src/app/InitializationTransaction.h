#pragma once

#include "app/NativeCallbackBoundary.h"
#include "app/ScopeExit.h"

namespace lholo::app {
// The caller has already acquired lifecycle ownership. Rollback runs once on
// an explicit failure or exception; success alone transfers that ownership.
template <class Initialize, class Rollback, class Failure>
bool initializeWithRollback(Initialize&& initialize, Rollback&& rollback, Failure&& failure) noexcept {
    static_assert(std::is_nothrow_invocable_v<Rollback>);
    bool committed{};
    ScopeExit cleanup([&]() noexcept { if (!committed) std::invoke(rollback); });
    auto const completed = invokeNativeCallback([&] {
        committed = std::invoke(std::forward<Initialize>(initialize));
    }, std::forward<Failure>(failure));
    return completed && committed;
}
} // namespace lholo::app
