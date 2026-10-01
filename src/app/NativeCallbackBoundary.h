#pragma once
#include <exception>
#include <functional>
#include <type_traits>
#include <utility>

namespace lholo::app {

void reportNativeCallbackFailure(char const* context, char const* reason) noexcept;

template <class Task, class Failure>
bool invokeNativeCallback(Task&& task, Failure&& failure) noexcept {
    static_assert(std::is_nothrow_invocable_v<Failure, char const*>);
    try {
        std::invoke(std::forward<Task>(task));
        return true;
    } catch (std::exception const& exception) {
        std::invoke(std::forward<Failure>(failure), exception.what());
    } catch (...) {
        std::invoke(std::forward<Failure>(failure), "unknown C++ exception");
    }
    return false;
}

} // namespace lholo::app
