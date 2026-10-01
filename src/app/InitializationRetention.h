#pragma once

#include "app/InitializationTransaction.h"

#include <memory>
#include <type_traits>
#include <utility>

namespace lholo::app {
// A loader may release its module owner after enable returns false. Failed
// cleanup must retain that owner before returning, including its library and
// diagnostics, while callbacks can still enter the module.
template <class Owner, class Initialize, class Rollback, class Failure>
bool initializeWithRetainedRollback(
    std::shared_ptr<Owner> const& owner,
    std::shared_ptr<Owner>& retained,
    Initialize&& initialize,
    Rollback&& rollback,
    Failure&& failure
) noexcept {
    static_assert(std::is_nothrow_invocable_r_v<bool, Rollback>);
    return initializeWithRollback(std::forward<Initialize>(initialize), [&]() noexcept {
        if (!std::invoke(rollback)) retained = owner;
    }, std::forward<Failure>(failure));
}
} // namespace lholo::app
