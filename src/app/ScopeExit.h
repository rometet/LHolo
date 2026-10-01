#pragma once
#include <type_traits>
#include <utility>

namespace lholo::app {
template <class Cleanup>
class ScopeExit {
public:
    explicit ScopeExit(Cleanup cleanup) : mCleanup(std::move(cleanup)) {
        static_assert(std::is_nothrow_invocable_v<Cleanup>);
    }
    ~ScopeExit() { mCleanup(); }
    ScopeExit(ScopeExit const&) = delete;
    ScopeExit& operator=(ScopeExit const&) = delete;
private:
    Cleanup mCleanup;
};
} // namespace lholo::app
